// SPDX-License-Identifier: GPL-2.0-or-later
//
// Configuration page for CtPU (Conversion to Physical Units), the math-stack,
// and XY multi-curve (TZ §12.1). The page is added as a fourth tab in the
// DsoConfigDialog. It is organised as a three-tab QTabWidget:
//   - CtPU tab:   per physical channel (CH1, CH2) — mode, k, b, unit, Zero/Span.
//   - Math tab:   M1..M4 — srcA, srcB, op, invert, enabled, unit, k, b.
//   - XY tab:     curve 0..3 — xChannel, yChannel, enabled.
//
// All controls write back to `settings->scope` in saveSettings(). The page is
// purely a UI shell — no validation beyond what the spinbox ranges enforce.
// Hot-path code reads the same `scope->voltage[]`, `scope->mathStack[]`, and
// `scope->xyCurves[]` vectors that this page edits.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QWidget>
#include <vector>

struct DsoSettings;

class DsoConfigCtpuMathPage : public QWidget {
    Q_OBJECT
  public:
    explicit DsoConfigCtpuMathPage( DsoSettings *settings, QWidget *parent = nullptr );
  public slots:
    void saveSettings();

  private:
    DsoSettings *settings;
    QTabWidget *tabWidget;
    QWidget *ctpuTab;
    QWidget *mathTab;
    QWidget *xyTab;

    // CtPU tab — per physical channel controls (size = 2 for CH1+CH2).
    struct CtpuChannelUI {
        QComboBox *modeCombo = nullptr;
        QLineEdit *unitEdit = nullptr;
        QDoubleSpinBox *kSpin = nullptr;
        QDoubleSpinBox *bSpin = nullptr;
        QDoubleSpinBox *zeroVSpin = nullptr;
        QDoubleSpinBox *spanVSpin = nullptr;
        QDoubleSpinBox *spanPhysSpin = nullptr;
        QPushButton *zeroButton = nullptr;
        QPushButton *spanButton = nullptr;
        QLabel *kLabel = nullptr;
        QLabel *bLabel = nullptr;
    };
    std::vector< CtpuChannelUI > ctpuUI;

    // Math tab — per math channel controls (size = maxMathChannels = 4).
    struct MathChannelUI {
        QCheckBox *enabledCheck = nullptr;
        QSpinBox *srcASpin = nullptr;
        QSpinBox *srcBSpin = nullptr;
        QComboBox *opCombo = nullptr;
        QCheckBox *invertCheck = nullptr;
        QLineEdit *unitEdit = nullptr;
        QDoubleSpinBox *kSpin = nullptr;
        QDoubleSpinBox *bSpin = nullptr;
    };
    std::vector< MathChannelUI > mathUI;

    // XY tab — per curve controls (size = maxXYCurves = 4).
    struct XYCurveUI {
        QCheckBox *enabledCheck = nullptr;
        QSpinBox *xChannelSpin = nullptr;
        QSpinBox *yChannelSpin = nullptr;
        QLabel *previewLabel = nullptr;
    };
    std::vector< XYCurveUI > xyUI;

    void buildCtpuTab( unsigned realChannelCount );
    void buildMathTab();
    void buildXYTab();
    void updateCtpuLabels();
};
