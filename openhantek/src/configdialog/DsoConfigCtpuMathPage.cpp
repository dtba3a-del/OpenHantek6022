// SPDX-License-Identifier: GPL-2.0-or-later

#include "DsoConfigCtpuMathPage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ctpu.h"
#include "dsosettings.h"
#include "scopesettings.h"


DsoConfigCtpuMathPage::DsoConfigCtpuMathPage( DsoSettings *settings, QWidget *parent )
    : QWidget( parent ), settings( settings ) {
    QVBoxLayout *mainLayout = new QVBoxLayout( this );
    tabWidget = new QTabWidget( this );

    // Determine real channel count from the device spec.
    const unsigned realChannels = unsigned( settings->scope.voltage.size() - DsoSettingsScope::maxMathChannels );

    ctpuTab = new QWidget();
    mathTab = new QWidget();
    xyTab = new QWidget();

    buildCtpuTab( realChannels );
    buildMathTab();
    buildXYTab();

    tabWidget->addTab( ctpuTab, tr( "CtPU" ) );
    tabWidget->addTab( mathTab, tr( "Math" ) );
    tabWidget->addTab( xyTab, tr( "XY" ) );

    mainLayout->addWidget( tabWidget );
    setLayout( mainLayout );
}


void DsoConfigCtpuMathPage::buildCtpuTab( unsigned realChannelCount ) {
    QVBoxLayout *outer = new QVBoxLayout( ctpuTab );
    QGridLayout *grid = new QGridLayout();
    grid->setSpacing( 6 );

    // Header row
    grid->addWidget( new QLabel( tr( "Channel" ) ), 0, 0 );
    grid->addWidget( new QLabel( tr( "Mode" ) ), 0, 1 );
    grid->addWidget( new QLabel( tr( "Unit" ) ), 0, 2 );
    grid->addWidget( new QLabel( tr( "k" ) ), 0, 3 );
    grid->addWidget( new QLabel( tr( "b" ) ), 0, 4 );
    grid->addWidget( new QLabel( tr( "Zero V" ) ), 0, 5 );
    grid->addWidget( new QLabel( tr( "Span V" ) ), 0, 6 );
    grid->addWidget( new QLabel( tr( "Span Phys" ) ), 0, 7 );
    grid->addWidget( new QLabel( tr( "Actions" ) ), 0, 8 );

    ctpuUI.clear();
    ctpuUI.resize( realChannelCount );
    for ( unsigned ch = 0; ch < realChannelCount; ++ch ) {
        const int row = int( ch ) + 1;
        auto &ui = ctpuUI[ ch ];

        grid->addWidget( new QLabel( settings->scope.voltage[ ch ].name ), row, 0 );

        ui.modeCombo = new QComboBox();
        ui.modeCombo->addItem( tr( "OFF" ), int( CtPU::Mode::OFF ) );
        ui.modeCombo->addItem( tr( "FORMULA" ), int( CtPU::Mode::FORMULA ) );
        ui.modeCombo->addItem( tr( "CCTPU" ), int( CtPU::Mode::CCTPU ) );
        grid->addWidget( ui.modeCombo, row, 1 );

        ui.unitEdit = new QLineEdit();
        ui.unitEdit->setMaxLength( 8 );
        ui.unitEdit->setPlaceholderText( QStringLiteral( "V" ) );
        grid->addWidget( ui.unitEdit, row, 2 );

        ui.kSpin = new QDoubleSpinBox();
        ui.kSpin->setRange( -1e9, 1e9 );
        ui.kSpin->setDecimals( 6 );
        ui.kSpin->setSingleStep( 0.001 );
        grid->addWidget( ui.kSpin, row, 3 );

        ui.bSpin = new QDoubleSpinBox();
        ui.bSpin->setRange( -1e9, 1e9 );
        ui.bSpin->setDecimals( 6 );
        ui.bSpin->setSingleStep( 0.001 );
        grid->addWidget( ui.bSpin, row, 4 );

        ui.zeroVSpin = new QDoubleSpinBox();
        ui.zeroVSpin->setRange( -1e3, 1e3 );
        ui.zeroVSpin->setDecimals( 6 );
        grid->addWidget( ui.zeroVSpin, row, 5 );

        ui.spanVSpin = new QDoubleSpinBox();
        ui.spanVSpin->setRange( -1e3, 1e3 );
        ui.spanVSpin->setDecimals( 6 );
        grid->addWidget( ui.spanVSpin, row, 6 );

        ui.spanPhysSpin = new QDoubleSpinBox();
        ui.spanPhysSpin->setRange( -1e9, 1e9 );
        ui.spanPhysSpin->setDecimals( 6 );
        grid->addWidget( ui.spanPhysSpin, row, 7 );

        ui.zeroButton = new QPushButton( tr( "Zero" ) );
        ui.spanButton = new QPushButton( tr( "Span" ) );
        ui.kLabel = new QLabel();
        ui.bLabel = new QLabel();
        QHBoxLayout *actionLayout = new QHBoxLayout();
        actionLayout->addWidget( ui.zeroButton );
        actionLayout->addWidget( ui.spanButton );
        actionLayout->addWidget( ui.kLabel );
        actionLayout->addWidget( ui.bLabel );
        QWidget *actionWidget = new QWidget();
        actionWidget->setLayout( actionLayout );
        grid->addWidget( actionWidget, row, 8 );

        // Wire Zero/Span buttons: they store the current spin values and
        // recompute (k, b) via CtPU::calculateFromCalibration().
        connect( ui.zeroButton, &QPushButton::clicked, this, [ this, ch ]() {
            if ( ch >= ctpuUI.size() )
                return;
            // For the stub we just take zeroVSpin's current value as V0.
            // A real implementation would average the live channel data here.
            double k = settings->scope.voltage[ ch ].ctpuK;
            double b = settings->scope.voltage[ ch ].ctpuB;
            const double zeroV = ctpuUI[ ch ].zeroVSpin->value();
            const double spanV = ctpuUI[ ch ].spanVSpin->value();
            const double spanP = ctpuUI[ ch ].spanPhysSpin->value();
            if ( CtPU::calculateFromCalibration( zeroV, spanV, spanP, k, b ) ) {
                ctpuUI[ ch ].kSpin->setValue( k );
                ctpuUI[ ch ].bSpin->setValue( b );
                updateCtpuLabels();
            }
        } );
        connect( ui.spanButton, &QPushButton::clicked, this, [ this, ch ]() {
            // Same as Zero — both buttons trigger (re)computation of k, b.
            if ( ch >= ctpuUI.size() )
                return;
            double k = settings->scope.voltage[ ch ].ctpuK;
            double b = settings->scope.voltage[ ch ].ctpuB;
            const double zeroV = ctpuUI[ ch ].zeroVSpin->value();
            const double spanV = ctpuUI[ ch ].spanVSpin->value();
            const double spanP = ctpuUI[ ch ].spanPhysSpin->value();
            if ( CtPU::calculateFromCalibration( zeroV, spanV, spanP, k, b ) ) {
                ctpuUI[ ch ].kSpin->setValue( k );
                ctpuUI[ ch ].bSpin->setValue( b );
                updateCtpuLabels();
            }
        } );
    }
    outer->addLayout( grid );
    outer->addStretch();

    // Pre-fill UI from settings.
    for ( unsigned ch = 0; ch < realChannelCount && ch < settings->scope.voltage.size(); ++ch ) {
        const auto &v = settings->scope.voltage[ ch ];
        ctpuUI[ ch ].modeCombo->setCurrentIndex( int( v.ctpuMode ) );
        ctpuUI[ ch ].unitEdit->setText( v.ctpuUnit );
        ctpuUI[ ch ].kSpin->setValue( v.ctpuK );
        ctpuUI[ ch ].bSpin->setValue( v.ctpuB );
        ctpuUI[ ch ].zeroVSpin->setValue( v.ccptuZeroV );
        ctpuUI[ ch ].spanVSpin->setValue( v.ccptuSpanV );
        ctpuUI[ ch ].spanPhysSpin->setValue( v.ccptuSpanPhysical );
    }
    updateCtpuLabels();
}


void DsoConfigCtpuMathPage::buildMathTab() {
    QVBoxLayout *outer = new QVBoxLayout( mathTab );
    QGridLayout *grid = new QGridLayout();
    grid->setSpacing( 6 );

    // Header row
    grid->addWidget( new QLabel( tr( "Math" ) ), 0, 0 );
    grid->addWidget( new QLabel( tr( "Enabled" ) ), 0, 1 );
    grid->addWidget( new QLabel( tr( "srcA" ) ), 0, 2 );
    grid->addWidget( new QLabel( tr( "srcB" ) ), 0, 3 );
    grid->addWidget( new QLabel( tr( "Op" ) ), 0, 4 );
    grid->addWidget( new QLabel( tr( "Invert" ) ), 0, 5 );
    grid->addWidget( new QLabel( tr( "Unit" ) ), 0, 6 );
    grid->addWidget( new QLabel( tr( "k" ) ), 0, 7 );
    grid->addWidget( new QLabel( tr( "b" ) ), 0, 8 );

    mathUI.clear();
    mathUI.resize( DsoSettingsScope::maxMathChannels );
    for ( int i = 0; i < DsoSettingsScope::maxMathChannels; ++i ) {
        const int row = i + 1;
        auto &ui = mathUI[ i ];

        grid->addWidget( new QLabel( QStringLiteral( "M%1" ).arg( i + 1 ) ), row, 0 );

        ui.enabledCheck = new QCheckBox();
        grid->addWidget( ui.enabledCheck, row, 1 );

        ui.srcASpin = new QSpinBox();
        ui.srcASpin->setRange( 0, int( settings->scope.voltage.size() ) - 1 );
        grid->addWidget( ui.srcASpin, row, 2 );

        ui.srcBSpin = new QSpinBox();
        ui.srcBSpin->setRange( 0, int( settings->scope.voltage.size() ) - 1 );
        grid->addWidget( ui.srcBSpin, row, 3 );

        ui.opCombo = new QComboBox();
        ui.opCombo->addItem( QStringLiteral( "+" ), int( Dso::MathOp::ADD ) );
        ui.opCombo->addItem( QStringLiteral( "-" ), int( Dso::MathOp::SUB ) );
        ui.opCombo->addItem( QStringLiteral( "*" ), int( Dso::MathOp::MUL ) );
        ui.opCombo->addItem( QStringLiteral( "/" ), int( Dso::MathOp::DIV ) );
        grid->addWidget( ui.opCombo, row, 4 );

        ui.invertCheck = new QCheckBox();
        grid->addWidget( ui.invertCheck, row, 5 );

        ui.unitEdit = new QLineEdit();
        ui.unitEdit->setMaxLength( 8 );
        ui.unitEdit->setPlaceholderText( QStringLiteral( "V" ) );
        grid->addWidget( ui.unitEdit, row, 6 );

        ui.kSpin = new QDoubleSpinBox();
        ui.kSpin->setRange( -1e9, 1e9 );
        ui.kSpin->setDecimals( 6 );
        ui.kSpin->setSingleStep( 0.001 );
        grid->addWidget( ui.kSpin, row, 7 );

        ui.bSpin = new QDoubleSpinBox();
        ui.bSpin->setRange( -1e9, 1e9 );
        ui.bSpin->setDecimals( 6 );
        ui.bSpin->setSingleStep( 0.001 );
        grid->addWidget( ui.bSpin, row, 8 );
    }
    outer->addLayout( grid );
    outer->addStretch();

    // Pre-fill from settings.
    for ( int i = 0; i < DsoSettingsScope::maxMathChannels && i < int( settings->scope.mathStack.size() ); ++i ) {
        const auto &m = settings->scope.mathStack[ i ];
        mathUI[ i ].enabledCheck->setChecked( m.enabled );
        mathUI[ i ].srcASpin->setValue( m.srcA );
        mathUI[ i ].srcBSpin->setValue( m.srcB );
        mathUI[ i ].opCombo->setCurrentIndex( int( m.op ) );
        mathUI[ i ].invertCheck->setChecked( m.invert );
        mathUI[ i ].unitEdit->setText( m.ctpuUnit );
        mathUI[ i ].kSpin->setValue( m.ctpuK );
        mathUI[ i ].bSpin->setValue( m.ctpuB );
    }
}


void DsoConfigCtpuMathPage::buildXYTab() {
    QVBoxLayout *outer = new QVBoxLayout( xyTab );
    QGridLayout *grid = new QGridLayout();
    grid->setSpacing( 6 );

    grid->addWidget( new QLabel( tr( "Curve" ) ), 0, 0 );
    grid->addWidget( new QLabel( tr( "Enabled" ) ), 0, 1 );
    grid->addWidget( new QLabel( tr( "X channel" ) ), 0, 2 );
    grid->addWidget( new QLabel( tr( "Y channel" ) ), 0, 3 );
    grid->addWidget( new QLabel( tr( "Preview" ) ), 0, 4 );

    xyUI.clear();
    xyUI.resize( DsoSettingsScope::maxXYCurves );
    for ( int i = 0; i < DsoSettingsScope::maxXYCurves; ++i ) {
        const int row = i + 1;
        auto &ui = xyUI[ i ];

        grid->addWidget( new QLabel( QStringLiteral( "Curve %1" ).arg( i ) ), row, 0 );

        ui.enabledCheck = new QCheckBox();
        grid->addWidget( ui.enabledCheck, row, 1 );

        ui.xChannelSpin = new QSpinBox();
        ui.xChannelSpin->setRange( 0, int( settings->scope.voltage.size() ) - 1 );
        grid->addWidget( ui.xChannelSpin, row, 2 );

        ui.yChannelSpin = new QSpinBox();
        ui.yChannelSpin->setRange( 0, int( settings->scope.voltage.size() ) - 1 );
        grid->addWidget( ui.yChannelSpin, row, 3 );

        ui.previewLabel = new QLabel();
        grid->addWidget( ui.previewLabel, row, 4 );

        // Update the preview label whenever the channel selections change.
        auto updatePreview = [ this, i ]() {
            if ( i >= int( xyUI.size() ) )
                return;
            const int x = xyUI[ i ].xChannelSpin->value();
            const int y = xyUI[ i ].yChannelSpin->value();
            if ( x < int( settings->scope.voltage.size() ) && y < int( settings->scope.voltage.size() ) ) {
                const QString xName = settings->scope.voltage[ x ].name;
                const QString yName = settings->scope.voltage[ y ].name;
                const QString xUnit = settings->scope.voltage[ x ].ctpuUnit;
                const QString yUnit = settings->scope.voltage[ y ].ctpuUnit;
                const double xGain = settings->scope.physicalGain( x );
                const double yGain = settings->scope.physicalGain( y );
                // [FIX] Two bugs: (1) the label used yName for the whole
                // string regardless of which channel was actually assigned
                // to X — every curve showed "CH2(...)" even when X was CH1,
                // M1, or anything else. (2) "дел" was a hardcoded Russian
                // literal bypassing tr() entirely — an English/German/etc.
                // build would show Russian text mixed into an otherwise
                // translated UI. Now shows both channel names explicitly and
                // reuses the same tr("/div") suffix already used in
                // dsowidget.cpp's gain label, so it participates in the
                // normal .ts translation files instead of hardcoding one language.
                xyUI[ i ].previewLabel->setText(
                    tr( "%1 vs %2  (x=%3 %4%5; y=%6 %7%5)" )
                        .arg( xName )
                        .arg( yName )
                        .arg( xGain )
                        .arg( xUnit )
                        .arg( tr( "/div" ) )
                        .arg( yGain )
                        .arg( yUnit ) );
            }
        };
        connect( ui.xChannelSpin, QOverload< int >::of( &QSpinBox::valueChanged ), this, updatePreview );
        connect( ui.yChannelSpin, QOverload< int >::of( &QSpinBox::valueChanged ), this, updatePreview );
    }
    outer->addLayout( grid );
    outer->addStretch();

    // Pre-fill from settings.
    for ( int i = 0; i < DsoSettingsScope::maxXYCurves && i < int( settings->scope.xyCurves.size() ); ++i ) {
        const auto &c = settings->scope.xyCurves[ i ];
        xyUI[ i ].enabledCheck->setChecked( c.enabled );
        xyUI[ i ].xChannelSpin->setValue( c.xChannel );
        xyUI[ i ].yChannelSpin->setValue( c.yChannel );
    }
}


void DsoConfigCtpuMathPage::updateCtpuLabels() {
    for ( size_t ch = 0; ch < ctpuUI.size(); ++ch ) {
        if ( !ctpuUI[ ch ].kLabel || !ctpuUI[ ch ].bLabel )
            continue;
        const double k = ctpuUI[ ch ].kSpin->value();
        const double b = ctpuUI[ ch ].bSpin->value();
        const QString unit = ctpuUI[ ch ].unitEdit->text().isEmpty() ? QStringLiteral( "V" ) : ctpuUI[ ch ].unitEdit->text();
        ctpuUI[ ch ].kLabel->setText( QStringLiteral( "k=%1 %2/V" ).arg( k, 0, 'f', 3 ).arg( unit ) );
        ctpuUI[ ch ].bLabel->setText( QStringLiteral( "b=%1 %2" ).arg( b, 0, 'f', 3 ).arg( unit ) );
    }
}


void DsoConfigCtpuMathPage::saveSettings() {
    // CtPU
    for ( size_t ch = 0; ch < ctpuUI.size() && ch < settings->scope.voltage.size(); ++ch ) {
        auto &v = settings->scope.voltage[ ch ];
        v.ctpuMode = CtPU::Mode( ctpuUI[ ch ].modeCombo->currentData().toInt() );
        v.ctpuUnit = ctpuUI[ ch ].unitEdit->text().isEmpty() ? QStringLiteral( "V" ) : ctpuUI[ ch ].unitEdit->text();
        v.ctpuK = ctpuUI[ ch ].kSpin->value();
        v.ctpuB = ctpuUI[ ch ].bSpin->value();
        v.ccptuZeroV = ctpuUI[ ch ].zeroVSpin->value();
        v.ccptuSpanV = ctpuUI[ ch ].spanVSpin->value();
        v.ccptuSpanPhysical = ctpuUI[ ch ].spanPhysSpin->value();
    }
    // Math
    settings->scope.mathStack.resize( DsoSettingsScope::maxMathChannels );
    for ( int i = 0; i < DsoSettingsScope::maxMathChannels; ++i ) {
        auto &m = settings->scope.mathStack[ i ];
        m.enabled = mathUI[ i ].enabledCheck->isChecked();
        m.srcA = uint8_t( mathUI[ i ].srcASpin->value() );
        m.srcB = uint8_t( mathUI[ i ].srcBSpin->value() );
        m.op = Dso::MathOp( mathUI[ i ].opCombo->currentData().toUInt() );
        m.invert = mathUI[ i ].invertCheck->isChecked();
        m.ctpuUnit = mathUI[ i ].unitEdit->text().isEmpty() ? QStringLiteral( "V" ) : mathUI[ i ].unitEdit->text();
        m.ctpuK = mathUI[ i ].kSpin->value();
        m.ctpuB = mathUI[ i ].bSpin->value();
    }
    // XY
    settings->scope.xyCurves.resize( DsoSettingsScope::maxXYCurves );
    for ( int i = 0; i < DsoSettingsScope::maxXYCurves; ++i ) {
        auto &c = settings->scope.xyCurves[ i ];
        c.enabled = xyUI[ i ].enabledCheck->isChecked();
        c.xChannel = uint8_t( xyUI[ i ].xChannelSpin->value() );
        c.yChannel = uint8_t( xyUI[ i ].yChannelSpin->value() );
    }
}
