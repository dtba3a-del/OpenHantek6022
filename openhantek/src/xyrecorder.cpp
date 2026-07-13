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
            tapeStream << "X(CH1),Y(CH2),Sigma\n";
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

    const unsigned masterChannel = config.masterAxis == MasterAxis::X ? 0u : 1u;
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

    const DataChannel *chX = data->data( 0 ); // CH1 -> X
    const DataChannel *chY = data->data( 1 ); // CH2 -> Y
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
        for ( std::size_t ch = 0; ch < scope->voltage.size() && ch < 2; ++ch ) {
            const auto &v = scope->voltage[ ch ];
            out << "# channel" << ch << ": name=" << v.name << " gain_V_per_div=" << scope->gain( unsigned( ch ) )
                << " offset_div=" << v.offset << " probeAttn=" << v.probeAttn
                << " inverted=" << ( v.inverted ? "true" : "false" );
            if ( spec )
                out << " coupling=" << Dso::couplingString( scope->coupling( ChannelID( ch ), spec ) );
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
