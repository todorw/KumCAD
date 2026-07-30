#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// LENGTHTUNE (KiCad's track length tuning tool): meanders the selected
// TrackEntity's longest straight segment to reach a target total length.
// Prompts for target length, then meander amplitude, then pitch, then an
// optional max amplitude (Enter = unbounded) -- see core/pcb/
// LengthTuning.h for the actual geometry, including the disclosed "picks
// the longest segment automatically" simplification vs. a real tuner's
// interactive placement, and maxAmplitude's own role as the caller-side
// clearance cap that function has no built-in awareness of.
class LengthTuneCommand : public DrawCommand {
public:
    LengthTuneCommand(lcad::Document& document, lcad::EntityId trackId)
        : m_document(document), m_trackId(trackId) {}

    QString start() override;
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    void apply(double pitch);

    lcad::Document& m_document;
    lcad::EntityId m_trackId;
    int m_stage = 0; // 0 = target length, 1 = amplitude, 2 = pitch, 3 = max amplitude
    double m_targetLength = 0.0;
    double m_amplitude = 0.5;
    double m_pitch = 1.0;
    double m_maxAmplitude = 0.0;
    std::optional<QString> m_result;
    bool m_finished = false;
};
