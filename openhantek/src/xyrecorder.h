// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QFile>
#include <QString>
#include <QTextStream>
#include <cstddef>
#include <deque>
#include <vector>

class PPresult;
struct DsoSettingsScope;

namespace Dso {
struct ControlSpecification;
}

/// \brief Continuous XY chart-recorder with anti-aliased cascade decimation.
///
/// Pen-plotter model: X is typically a sawtooth sweep (0.0001 V/s .. 10 V/us),
/// Y is the response, independent slew range. Points arrive as (CH1,CH2) pairs
/// and are pushed through a cascade of small averaging FIFO stages: each stage
/// low-pass-filters (box-car mean) its input *before* decimating to the next
/// stage. This avoids the aliasing/steps that plain stride-decimation produces,
/// while memory stays bounded to (cascade depth x stage size), independent of
/// recording length.
///
/// Two sizing modes ("лист" vs "лента"):
///   FINITE - operator knows the full-scale range of the master axis (comes
///            from DsoSettingsScope::gain() for that channel) and an expected
///            slew rate -> sweep duration is estimated, cascade is sized to
///            hit targetPoints for the whole sweep. Fits in RAM by design.
///   TAPE   - open-ended recording, cascade is sized to hit a fixed output
///            density (targetDensity points/second) instead. Since the total
///            length is unbounded, points are streamed to disk in chunks as
///            they age out of the in-RAM render window (tapeFilePath) -
///            nothing is discarded, RAM only ever holds the current window.
class XYRecorder {
  public:
    enum class SheetMode { FINITE, TAPE };        ///< Фиксированное N (лист) vs фиксированная плотность (лента)
    enum class MasterAxis { X, Y };                ///< Which axis' full-scale/slew rate sizes the cascade
    enum class ExtractMode { CASCADE, PEAK_ENVELOPE }; ///< Mean per block, or min+max envelope per block

    struct Point {
        double x = 0.0;
        double y = 0.0;
        double sigma = 0.0; ///< std-dev of the final cascade block (0 if trackSigma == false)
    };

    struct Config {
        SheetMode sheetMode = SheetMode::FINITE;
        MasterAxis masterAxis = MasterAxis::X;

        double slewRateX = 1.0; ///< V/s, expected range 1e-4 .. 1e7 (10 V/us)
        double slewRateY = 1.0; ///< V/s, same range

        std::size_t targetPoints = 2000; ///< FINITE: total points for the whole sweep
        double targetDensity = 2000.0;   ///< TAPE: points per second

        int cascadeBase = 8;             ///< samples averaged per stage, every stage
        ExtractMode extractMode = ExtractMode::CASCADE;
        bool trackSigma = false;         ///< only meaningful at the last cascade stage

        /// TAPE only. Empty = no persistence, points are dropped from the
        /// front once the render window is exceeded (old ring-buffer
        /// behaviour, data IS lost - only useful for a quick live preview).
        /// Non-empty = every point that ages out of the render window is
        /// appended to this CSV instead of being dropped; full record is
        /// preserved on disk while RAM stays bounded to renderWindowPoints.
        QString tapeFilePath;

        /// Points kept in RAM for GPU rendering (both modes). For TAPE with
        /// a tapeFilePath set, this is also the trigger for flushing the
        /// oldest chunk to disk.
        std::size_t renderWindowPoints = 200000;

        /// TAPE + tapeFilePath only: how many of the oldest points to flush
        /// to disk at once. 0 = auto (renderWindowPoints / 2).
        std::size_t flushChunkPoints = 0;

        /// Safety net only, not the primary sizing mechanism. 0 = auto:
        /// 10x targetPoints (FINITE) or renderWindowPoints (TAPE without
        /// tapeFilePath).
        std::size_t safetyCapPoints = 0;
    };

    XYRecorder() = default;
    ~XYRecorder();

    /// Sizes the cascade from scope->horizontal.samplerate and
    /// scope->gain(masterChannel), clears any previous trajectory, and (TAPE
    /// + tapeFilePath) opens the output file and writes its header. Not safe
    /// mid-recording - call before starting acquisition. Any previous
    /// streaming file is finalized (flushed + closed) first. scope/spec are
    /// non-owning and must outlive the recorder (both owned by DsoWidget).
    void configure( DsoSettingsScope *scope, const Dso::ControlSpecification *spec, const Config &cfg );

    /// Feed one PPresult frame (many raw samples at hardware samplerate).
    void addFrame( const PPresult *data );

    /// Flushes any buffered points to the streaming file (if open) and
    /// closes it, then clears the in-RAM trajectory. Call when stopping
    /// acquisition - also called from the destructor as a safety net.
    void finalize();

    /// Clears the in-RAM trajectory only. Does NOT flush/close a streaming
    /// file - call finalize() first if one is open, or those points are lost.
    void clear();

    const std::deque< Point > &trajectory() const { return traj; }

    bool isStreamingToDisk() const { return tapeFile.isOpen(); }

    /// Snapshot of what's currently in RAM. In TAPE + streaming mode this is
    /// only the current render window (tail) - the full record is already at
    /// config.tapeFilePath. In FINITE mode this is the whole recording.
    void exportCSV( const QString &filename ) const;
    void exportCSVDetailed( const QString &filename ) const;

    bool empty() const { return traj.empty(); }
    std::size_t size() const { return traj.size(); }

    const Config &currentConfig() const { return config; }
    std::size_t cascadeDepth() const { return cascade.size(); }

  private:
    struct CascadeStage {
        double sumX = 0.0, sumY = 0.0;
        double sumX2 = 0.0, sumY2 = 0.0; ///< only accumulated at the last stage when trackSigma
        double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
        int count = 0;
    };

    DsoSettingsScope *scope = nullptr;
    const Dso::ControlSpecification *spec = nullptr;
    Config config;
    std::vector< CascadeStage > cascade;
    std::deque< Point > traj;
    std::size_t safetyCap = 0;

    QFile tapeFile;
    QTextStream tapeStream;

    void rebuildCascade();
    void feedStage( std::size_t stageIndex, double x, double y, double sigma );
    void emitPoint( double x, double y, double sigma );
    void flushChunkToDisk( std::size_t count );
    void writeHeader( QTextStream &out ) const;
};
