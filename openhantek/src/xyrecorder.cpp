// SPDX-License-Identifier: GPL-2.0-or-later

#include "xyrecorder.h"

#include "hantekdso/controlspecification.h"
#include "post/ppresult.h"
#include "scopesettings.h"
#include "viewconstants.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

XYRecorder::~XYRecorder() { finalize(); }


void XYRecorder::configure( DsoSettingsScope *scopeIn, const Dso::ControlSpecification *specIn, const Config &cfg ) {
    finalize(); // close/flush any previous streaming file before reconfiguring

    scope = scopeIn;
    spec = specIn;
    config = cfg;
    rebuildCascade(); // clears cascade + traj

    if ( config.sheetMode == SheetMode::TAPE && !config.tapeFilePath.isEmpty() ) {
        tapeFile.setFileName( config.tapeFilePath );
        if ( tapeFile.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
            tapeStream.setDevice( &tapeFile );
            writeHeader( tapeStream );
            // [FIX] TZ §7.6.3 — column header reflects this recorder's bound
            // channels (from m_curveConfig), not the hardcoded CH1/CH2 pair.
            QString xName = QStringLiteral( "CH1" );
            QString yName = QStringLiteral( "CH2" );
            if ( scope && m_curveConfig.xChannel < scope->voltage.size() )
                xName = scope->voltage[ m_curveConfig.xChannel ].name;
            if ( scope && m_curveConfig.yChannel < scope->voltage.size() )
                yName = scope->voltage[ m_curveConfig.yChannel ].name;
            tapeStream << "X(" << xName << "),Y(" << yName << "),Sigma\n";
        }
        // If open() fails, tapeFile stays closed -> emitPoint() falls back to
        // the bounded-RAM ring behaviour (isStreamingToDisk() == false), so
        // recording still works, just without persistence. Caller should
        // check isStreamingToDisk() after configure() to warn the user.
    }
}


void XYRecorder::rebuildCascade() {
    cascade.clear();
    traj.clear();

    const double samplerate = scope ? scope->horizontal.samplerate : 1e6;

    const double slewRate =
        qBound( 1e-4, config.masterAxis == MasterAxis::X ? config.slewRateX : config.slewRateY, 1e7 );

    // [FIX] TZ §7.4 — use the channel actually bound to this curve's master
    // axis (via setCurveConfig()) instead of hardcoded CH1=0/CH2=1. Before
    // this fix, every curve's cascade depth (and therefore its point
    // density / smoothing) was sized from CH1's or CH2's V/div setting
    // regardless of which channels the curve was actually configured to
    // record — correct only by coincidence for the default CH1×CH2 curve 0.
    const unsigned masterChannel = config.masterAxis == MasterAxis::X ? m_curveConfig.xChannel : m_curveConfig.yChannel;
    const double fullScaleRange = ( scope && masterChannel < scope->voltage.size() )
                                       ? scope->gain( masterChannel ) * DIVS_VOLTAGE
                                       : DIVS_VOLTAGE; // fallback if scope not wired yet

    double decimationFactor;
    if ( config.sheetMode == SheetMode::FINITE ) {
        const double sweepDuration = fullScaleRange / slewRate; // s, master axis full-scale crossing
        const double targetPts = double( std::max< std::size_t >( 1, config.targetPoints ) );
        decimationFactor = ( samplerate * sweepDuration ) / targetPts;
    } else { // TAPE
        const double density = config.targetDensity > 0.0 ? config.targetDensity : 1.0;
        decimationFactor = samplerate / density;
    }
    decimationFactor = std::max( 1.0, decimationFactor );

    const int base = qMax( 2, config.cascadeBase );
    int depth = int( std::ceil( std::log( decimationFactor ) / std::log( double( base ) ) ) );
    depth = qMax( 1, depth );
    cascade.assign( std::size_t( depth ), CascadeStage() );

    safetyCap = config.safetyCapPoints;
    if ( safetyCap == 0 )
        safetyCap = ( config.sheetMode == SheetMode::FINITE ) ? config.targetPoints * 10 : config.renderWindowPoints;
}


void XYRecorder::addFrame( const PPresult *data ) {
    if ( !data || cascade.empty() )
        return;

    // [FIX] TZ §7.4 — use this recorder's bound (xChannel, yChannel) pair
    // instead of the hardcoded CH1=0/CH2=1. The binding is set by
    // DsoWidget::configureXYRecorder() via setCurveConfig() and reflects
    // scope->xyCurves[i] for this specific curve slot. Without this fix all
    // four recorders would record the same CH1×CH2 trajectory regardless of
    // the user's per-curve channel selection.
    const DataChannel *chX = data->data( m_curveConfig.xChannel );
    const DataChannel *chY = data->data( m_curveConfig.yChannel );
    if ( !chX || !chY )
        return;

    const auto &sx = chX->voltage.samples;
    const auto &sy = chY->voltage.samples;
    const std::size_t n = std::min( sx.size(), sy.size() );

    for ( std::size_t i = 0; i < n; ++i )
        feedStage( 0, sx[ i ], sy[ i ], 0.0 );
}


/// Box-car mean per stage, flushed to the next stage every cascadeBase
/// samples -> low-pass *before* decimation at every level, so the final
/// output stays smooth instead of aliasing (unlike plain stride-decimate).
void XYRecorder::feedStage( std::size_t stageIndex, double x, double y, double sigma ) {
    if ( stageIndex >= cascade.size() ) {
        emitPoint( x, y, sigma );
        return;
    }

    CascadeStage &st = cascade[ stageIndex ];
    const bool isFinal = ( stageIndex == cascade.size() - 1 );

    if ( st.count == 0 ) {
        st.minX = st.maxX = x;
        st.minY = st.maxY = y;
    }
    st.sumX += x;
    st.sumY += y;
    st.minX = std::min( st.minX, x );
    st.maxX = std::max( st.maxX, x );
    st.minY = std::min( st.minY, y );
    st.maxY = std::max( st.maxY, y );
    if ( isFinal && config.trackSigma ) {
        st.sumX2 += x * x;
        st.sumY2 += y * y;
    }
    ++st.count;

    if ( st.count < config.cascadeBase )
        return;

    const double meanX = st.sumX / st.count;
    const double meanY = st.sumY / st.count;

    double outSigma = 0.0;
    if ( isFinal && config.trackSigma ) {
        const double varX = std::max( 0.0, st.sumX2 / st.count - meanX * meanX );
        const double varY = std::max( 0.0, st.sumY2 / st.count - meanY * meanY );
        outSigma = std::sqrt( varX + varY );
    }

    if ( isFinal && config.extractMode == ExtractMode::PEAK_ENVELOPE ) {
        feedStage( stageIndex + 1, st.minX, st.minY, 0.0 );
        if ( st.maxX != st.minX || st.maxY != st.minY )
            feedStage( stageIndex + 1, st.maxX, st.maxY, 0.0 );
    } else {
        feedStage( stageIndex + 1, meanX, meanY, outSigma );
    }

    st = CascadeStage(); // reset for the next block
}


void XYRecorder::emitPoint( double x, double y, double sigma ) {
    traj.push_back( { x, y, sigma } );

    if ( tapeFile.isOpen() ) {
        // Streaming: once the render window is exceeded, flush the oldest
        // chunk to disk instead of dropping it - nothing is lost.
        const std::size_t chunk =
            config.flushChunkPoints ? config.flushChunkPoints : std::max< std::size_t >( 1, config.renderWindowPoints / 2 );
        if ( traj.size() > config.renderWindowPoints + chunk )
            flushChunkToDisk( chunk );
    } else if ( safetyCap && traj.size() > safetyCap ) {
        traj.pop_front(); // no streaming target configured -> bounded-RAM fallback, data IS lost
    }
}


void XYRecorder::flushChunkToDisk( std::size_t count ) {
    count = std::min( count, traj.size() );
    for ( std::size_t i = 0; i < count; ++i ) {
        const Point &p = traj[ i ];
        tapeStream << p.x << "," << p.y << "," << p.sigma << "\n";
    }
    traj.erase( traj.begin(), traj.begin() + long( count ) );
    tapeStream.flush();
}


void XYRecorder::finalize() {
    if ( tapeFile.isOpen() ) {
        flushChunkToDisk( traj.size() ); // write everything still buffered
        tapeStream.flush();
        tapeFile.close();
    }
}


void XYRecorder::clear() {
    traj.clear();
    for ( auto &st : cascade )
        st = CascadeStage();
}


void XYRecorder::writeHeader( QTextStream &out ) const {
    out << "# XY Recorder settings\n";
    out << "# sheetMode=" << ( config.sheetMode == SheetMode::FINITE ? "FINITE" : "TAPE" ) << "\n";
    out << "# masterAxis=" << ( config.masterAxis == MasterAxis::X ? "X" : "Y" ) << "\n";
    out << "# slewRateX_V_per_s=" << config.slewRateX << "\n";
    out << "# slewRateY_V_per_s=" << config.slewRateY << "\n";
    if ( config.sheetMode == SheetMode::FINITE )
        out << "# targetPoints=" << qulonglong( config.targetPoints ) << "\n";
    else
        out << "# targetDensity_pts_per_s=" << config.targetDensity << "\n";
    out << "# cascadeBase=" << config.cascadeBase << " cascadeDepth=" << qulonglong( cascade.size() ) << "\n";
    out << "# extractMode=" << ( config.extractMode == ExtractMode::PEAK_ENVELOPE ? "PEAK_ENVELOPE" : "CASCADE" )
        << "\n";
    out << "# trackSigma=" << ( config.trackSigma ? "true" : "false" ) << "\n";

    if ( scope ) {
        out << "# samplerate_Hz=" << scope->horizontal.samplerate << "\n";
        out << "# timebase_s_per_div=" << scope->horizontal.timebase << "\n";
        out << "# format=" << Dso::graphFormatString( scope->horizontal.format ) << "\n";
        out << "# trigger.mode=" << Dso::triggerModeString( scope->trigger.mode )
            << " slope=" << Dso::slopeString( scope->trigger.slope ) << " source=" << scope->trigger.source
            << " position=" << scope->trigger.position << "\n";
        // [FIX] TZ §7.6.3 — emit metadata for this recorder's bound X and Y
        // channels (from m_curveConfig), not the hardcoded CH1/CH2 pair.
        // This ensures the CSV header matches the actual data in the file
        // when exporting curves whose xChannel/yChannel differ from 0/1.
        const auto &cfg = m_curveConfig;
        if ( cfg.xChannel < scope->voltage.size() ) {
            const auto &v = scope->voltage[ cfg.xChannel ];
            out << "# X_channel: name=" << v.name << " gain=" << scope->physicalGain( cfg.xChannel ) << " "
                << v.ctpuUnit << "/div"
                << " offset_div=" << v.offset << " probeAttn=" << v.probeAttn
                << " inverted=" << ( v.inverted ? "true" : "false" );
            if ( spec && cfg.xChannel < spec->channels )
                out << " coupling=" << Dso::couplingString( scope->coupling( ChannelID( cfg.xChannel ), spec ) );
            out << "\n";
        }
        if ( cfg.yChannel < scope->voltage.size() ) {
            const auto &v = scope->voltage[ cfg.yChannel ];
            out << "# Y_channel: name=" << v.name << " gain=" << scope->physicalGain( cfg.yChannel ) << " "
                << v.ctpuUnit << "/div"
                << " offset_div=" << v.offset << " probeAttn=" << v.probeAttn
                << " inverted=" << ( v.inverted ? "true" : "false" );
            if ( spec && cfg.yChannel < spec->channels )
                out << " coupling=" << Dso::couplingString( scope->coupling( ChannelID( cfg.yChannel ), spec ) );
            out << "\n";
        }
    }
    out << "#\n";
}


void XYRecorder::exportCSV( const QString &filename ) const {
    QFile file( filename );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        return;
    QTextStream out( &file );
    writeHeader( out );
    // [FIX] TZ §7.6.3 — use this recorder's own m_curveConfig (bound via
    // setCurveConfig()) instead of scope->xyCurves[0]. Without this fix,
    // exporting curves 1–3 would label the columns with curve 0's channels.
    const auto &cfg = m_curveConfig;
    if ( scope && cfg.xChannel < scope->voltage.size() && cfg.yChannel < scope->voltage.size() ) {
        const auto &xv = scope->voltage[ cfg.xChannel ];
        const auto &yv = scope->voltage[ cfg.yChannel ];
        out << "# X: " << xv.name << ", Unit: " << xv.ctpuUnit << ", Gain: " << scope->physicalGain( cfg.xChannel ) << " "
            << xv.ctpuUnit << "/div\n";
        out << "# Y: " << yv.name << ", Unit: " << yv.ctpuUnit << ", Gain: " << scope->physicalGain( cfg.yChannel ) << " "
            << yv.ctpuUnit << "/div\n";
        out << "X,Y\n";
        for ( const auto &p : traj )
            out << p.x << "," << p.y << "\n";
        return;
    }
    // Fallback: legacy format (no metadata, with Sigma column).
    out << "X(CH1),Y(CH2),Sigma\n";
    for ( const auto &p : traj )
        out << p.x << "," << p.y << "," << p.sigma << "\n";
}


void XYRecorder::exportCSVDetailed( const QString &filename ) const {
    QFile file( filename );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        return;
    QTextStream out( &file );
    writeHeader( out );
    out << "Index,X,Y,Sigma\n";
    std::size_t idx = 0;
    for ( const auto &p : traj )
        out << idx++ << "," << p.x << "," << p.y << "," << p.sigma << "\n";
}
