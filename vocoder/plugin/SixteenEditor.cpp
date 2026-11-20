#include "SixteenEditor.h"

using namespace juce;
using namespace sixteen;
using vocoder::kNumChannels;
using vocoder::kPanelEdgesHz;

// ---------------------------------------------------------------------------
// Panel artwork helpers

namespace
{
const Colour kPanel     { 0xff0b0b0c };
const Colour kInk       { 0xffeeeeea };   // silk-screen white
const Colour kInkDim    { 0xff9a9a96 };

Font panelFont (float h, bool bold = true)
{
    return Font (FontOptions().withHeight (h).withStyle (bold ? "Bold" : "Regular"));
}

void text (Graphics& g, const String& s, float cx, float cy, float h, bool bold = true, Colour c = kInk, float w = 200)
{
    g.setColour (c);
    g.setFont (panelFont (h, bold));
    g.drawText (s, Rectangle<float> (cx - w / 2, cy - h, w, h * 2), Justification::centred, false);
}

// Bracket like "|---- PROGRAM ----|" with ends turned down
void bracket (Graphics& g, const String& label, float x0, float x1, float y, float h = 12)
{
    g.setFont (panelFont (h));
    const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 14;
    const float cx = (x0 + x1) / 2;
    g.setColour (kInk);
    g.fillRect (x0, y - 1.0f, cx - tw / 2 - x0, 2.0f);
    g.fillRect (cx + tw / 2, y - 1.0f, x1 - cx - tw / 2, 2.0f);
    g.fillRect (x0, y - 1.0f, 2.0f, 10.0f);
    g.fillRect (x1 - 2, y - 1.0f, 2.0f, 10.0f);
    text (g, label, cx, y, h);
}

// Chrome jack: hex nut, washer, black hole
void drawJack (Graphics& g, Point<float> c, bool highlight)
{
    const float r = 17;
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (c.x - r - 1, c.y - r + 2, 2 * r + 2, 2 * r + 2);

    ColourGradient metal (Colour (0xfff4f4f2), c.x - r, c.y - r, Colour (0xff6c6c6a), c.x + r, c.y + r, false);
    metal.addColour (0.45, Colour (0xffc9c9c6));
    g.setGradientFill (metal);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colour (0xff3a3a38));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.0f);

    // knurled nut ring
    const float rn = 13;
    ColourGradient nut (Colour (0xff8d8d8a), c.x + rn, c.y + rn, Colour (0xffe9e9e6), c.x - rn, c.y - rn, false);
    g.setGradientFill (nut);
    g.fillEllipse (c.x - rn, c.y - rn, 2 * rn, 2 * rn);
    g.setColour (Colour (0x55000000));
    for (int i = 0; i < 24; ++i)
    {
        const float a = (float) i * MathConstants<float>::twoPi / 24;
        g.drawLine (c.x + std::cos (a) * (rn - 2.5f), c.y + std::sin (a) * (rn - 2.5f), c.x + std::cos (a) * rn, c.y + std::sin (a) * rn, 0.8f);
    }

    const float rh = 7.5f;
    ColourGradient hole (Colour (0xff000000), c.x, c.y, Colour (0xff2a2a2a), c.x - rh, c.y - rh, true);
    g.setGradientFill (hole);
    g.fillEllipse (c.x - rh, c.y - rh, 2 * rh, 2 * rh);
    g.setColour (Colour (0xff555552));
    g.drawEllipse (c.x - rh, c.y - rh, 2 * rh, 2 * rh, 1.2f);

    if (highlight)
    {
        g.setColour (Colour (0xfffff2a0).withAlpha (0.8f));
        g.drawEllipse (c.x - r - 3, c.y - r - 3, 2 * r + 6, 2 * r + 6, 2.0f);
    }
}

void drawLed (Graphics& g, Point<float> c, Colour colour, float level, float r = 5.5f)
{
    level = jlimit (0.0f, 1.0f, level);
    g.setColour (Colour (0xff2b2b2b));
    g.fillEllipse (c.x - r - 2.5f, c.y - r - 2.5f, 2 * r + 5, 2 * r + 5);
    if (level > 0.02f)
    {
        ColourGradient glow (colour.withAlpha (0.55f * level), c.x, c.y, colour.withAlpha (0.0f), c.x + r * 4, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - r * 4, c.y - r * 4, r * 8, r * 8);
    }
    const Colour off = colour.darker (2.6f).withSaturation (0.6f);
    const Colour lit = off.interpolatedWith (colour.brighter (0.3f), level);
    ColourGradient body (lit.brighter (0.6f * level + 0.2f), c.x - r * 0.4f, c.y - r * 0.5f, lit, c.x + r, c.y + r, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colours::white.withAlpha (0.35f + 0.4f * level));
    g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.65f, r * 0.6f, r * 0.45f);
}

// Scale numbers around a knob
void knobScale (Graphics& g, Point<float> c, float radius, const StringArray& labels, float h = 10)
{
    const int n = labels.size();
    for (int i = 0; i < n; ++i)
    {
        const float a = Knob::angleFor ((float) i / (float) (n - 1));
        const Point<float> p (c.x + std::sin (a) * radius, c.y - std::cos (a) * radius);
        text (g, labels[i], p.x, p.y, h, true, kInk, 30);
    }
}
} // namespace

// ---------------------------------------------------------------------------
ParamControl::ParamControl (RangedAudioParameter& p)
    : param (p),
      attachment (p, [this] (float v) { current = v; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

// ---- Knob
void Knob::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const Point<float> c = b.getCentre();
    const float r = jmin (b.getWidth(), b.getHeight()) / 2 - 2;

    g.setColour (Colours::black.withAlpha (0.7f));
    g.fillEllipse (c.x - r, c.y - r + 3, 2 * r, 2 * r);

    // knurled black skirt
    ColourGradient skirt (Colour (0xff3c3c3c), c.x - r, c.y - r, Colour (0xff050505), c.x + r, c.y + r, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colour (0xff4a4a4a));
    for (int i = 0; i < 36; ++i)
    {
        const float a = (float) i * MathConstants<float>::twoPi / 36;
        g.drawLine (c.x + std::cos (a) * r * 0.78f, c.y + std::sin (a) * r * 0.78f, c.x + std::cos (a) * r, c.y + std::sin (a) * r, 1.0f);
    }

    // metal cap
    const float rc = r * 0.62f;
    ColourGradient cap (Colour (0xffffffff), c.x - rc, c.y - rc, Colour (0xff7d7d7a), c.x + rc, c.y + rc, false);
    cap.addColour (0.5, Colour (0xffcfcfcb));
    g.setGradientFill (cap);
    g.fillEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
    g.setColour (Colour (0x30000000));
    for (int i = 1; i < 6; ++i)
        g.drawEllipse (c.x - rc * i / 6, c.y - rc * i / 6, rc * i / 3, rc * i / 3, 0.6f);

    // pointer
    const float a = angleFor (param.convertTo0to1 (current));
    Path ptr;
    ptr.addRoundedRectangle (-1.6f, -r + 1, 3.2f, r * 0.95f, 1.5f);
    g.setColour (Colour (0xff101010));
    g.fillPath (ptr, AffineTransform::rotation (a).translated (c));
}

void Knob::mouseDown (const MouseEvent&)
{
    dragStartNorm = param.convertTo0to1 (current);
    attachment.beginGesture();
}

void Knob::mouseDrag (const MouseEvent& e)
{
    const float speed = e.mods.isShiftDown() ? 0.0008f : 0.004f;
    const float delta = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) * speed;
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, dragStartNorm + delta)));
}

void Knob::mouseUp (const MouseEvent&) { attachment.endGesture(); }

void Knob::mouseDoubleClick (const MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Knob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float norm = jlimit (0.0f, 1.0f, param.convertTo0to1 (current) + w.deltaY * 0.05f);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (norm));
}

// ---- Mode selector
void ModeSelector::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const Point<float> c = b.getCentre();
    const float r = jmin (b.getWidth(), b.getHeight()) / 2 - 2;
    g.setColour (Colours::black.withAlpha (0.7f));
    g.fillEllipse (c.x - r, c.y - r + 3, 2 * r, 2 * r);
    ColourGradient body (Colour (0xff4a4a4a), c.x - r, c.y - r, Colour (0xff080808), c.x + r, c.y + r, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    const float rc = r * 0.72f;
    ColourGradient cap (Colour (0xff2c2c2c), c.x, c.y - rc, Colour (0xff111111), c.x, c.y + rc, false);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
    // white pointer line
    const float a = angleFor ((int) std::lround (current));
    Path ptr;
    ptr.addRoundedRectangle (-1.8f, -r + 2, 3.6f, r * 0.8f, 1.5f);
    g.setColour (kInk);
    g.fillPath (ptr, AffineTransform::rotation (a).translated (c));
}

void ModeSelector::mouseDown (const MouseEvent& e)
{
    const float x = (float) e.x / (float) getWidth();
    const int pos = x < 0.36f ? 0 : (x > 0.64f ? 2 : 1);
    attachment.setValueAsCompleteGesture ((float) pos);
}

// ---- Toggles
void Toggle::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1);
    const bool on = isOn();

    switch (style)
    {
        case Style::Paddle:     // IN / OUT paddle (SAMPLE/HOLD, EXTERNAL PATCH, STATUS)
        {
            g.setColour (Colour (0xff3e3e3e));
            g.fillRoundedRectangle (b, 3);
            g.setColour (Colour (0xff8a8a88));
            g.drawRoundedRectangle (b, 3, 1.2f);
            auto inner = b.reduced (4);
            auto top = inner.removeFromTop (inner.getHeight() / 2);
            auto bottom = inner;
            auto drawHalf = [&g] (Rectangle<float> r, const String& label, bool pressed)
            {
                ColourGradient cg (pressed ? Colour (0xff1b1b1b) : Colour (0xff6c6c6a), r.getX(), r.getY(),
                                   pressed ? Colour (0xff2a2a2a) : Colour (0xff3a3a38), r.getX(), r.getBottom(), false);
                g.setGradientFill (cg);
                g.fillRect (r);
                g.setColour (pressed ? Colour (0xff8c8c88) : kInk);
                g.setFont (panelFont (9.5f));
                g.drawText (label, r, Justification::centred);
            };
            drawHalf (top, "IN", ! on);
            drawHalf (bottom, "OUT", on);
            g.setColour (Colours::black);
            g.drawLine (top.getX(), top.getBottom(), top.getRight(), top.getBottom(), 1.0f);
            break;
        }
        case Style::RockerVertical:   // MIC (top) / LINE (bottom): on = MIC
        case Style::Power:            // ON (top) / OFF (bottom)
        {
            g.setColour (Colour (0xff2c2c2c));
            g.fillRect (b);
            g.setColour (Colour (0xff777775));
            g.drawRect (b, 1.2f);
            auto inner = b.reduced (3);
            const bool topDown = on;
            auto top = inner.removeFromTop (inner.getHeight() / 2);
            auto bot = inner;
            g.setGradientFill (ColourGradient (Colour (topDown ? 0xff151515 : 0xff5a5a58), top.getX(), top.getY(),
                                               Colour (topDown ? 0xff222222 : 0xff3a3a38), top.getX(), top.getBottom(), false));
            g.fillRect (top);
            g.setGradientFill (ColourGradient (Colour (topDown ? 0xff3a3a38 : 0xff151515), bot.getX(), bot.getY(),
                                               Colour (topDown ? 0xff5a5a58 : 0xff222222), bot.getX(), bot.getBottom(), false));
            g.fillRect (bot);
            if (style == Style::Power)
            {
                g.setFont (panelFont (9.0f));
                g.setColour (kInk);
                g.drawText ("ON", top, Justification::centred);
                g.drawText ("OFF", bot, Justification::centred);
            }
            break;
        }
        case Style::RockerHorizontal:   // SWITCHED (left) / DIRECT (right): on = DIRECT
        {
            g.setColour (Colour (0xff2c2c2c));
            g.fillRect (b);
            g.setColour (Colour (0xff777775));
            g.drawRect (b, 1.2f);
            auto inner = b.reduced (3);
            auto left = inner.removeFromLeft (inner.getWidth() / 2);
            auto right = inner;
            g.setGradientFill (ColourGradient (Colour (on ? 0xff5a5a58 : 0xff151515), left.getX(), 0, Colour (on ? 0xff3a3a38 : 0xff222222), left.getRight(), 0, false));
            g.fillRect (left);
            g.setGradientFill (ColourGradient (Colour (on ? 0xff222222 : 0xff3a3a38), right.getX(), 0, Colour (on ? 0xff151515 : 0xff5a5a58), right.getRight(), 0, false));
            g.fillRect (right);
            break;
        }
    }
}

void Toggle::mouseDown (const MouseEvent& e)
{
    bool newOn = ! isOn();
    // rockers: clicking a half selects that half
    if (style == Style::RockerVertical || style == Style::Power)   newOn = e.y < getHeight() / 2;
    if (style == Style::RockerHorizontal)                          newOn = e.x > getWidth() / 2;
    if (style == Style::Paddle)                                    newOn = e.y < getHeight() / 2;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (newOn ? 1.0f : 0.0f));
}

// ---------------------------------------------------------------------------
// Panel

static RangedAudioParameter& P (SixteenProcessor& p, const char* id) { return *p.apvts.getParameter (id); }

Panel::Panel (SixteenProcessor& p)
    : proc (p),
      programLevel (P (p, "programLevel")), carrierLevel (P (p, "carrierLevel")), balance (P (p, "balance")),
      mode (P (p, "mode")),
      mic (P (p, "mic"), Toggle::Style::RockerVertical),
      sampleHold (P (p, "sampleHold"), Toggle::Style::Paddle),
      externalPatch (P (p, "externalPatch"), Toggle::Style::Paddle),
      hfDirect (P (p, "hfDirect"), Toggle::Style::RockerHorizontal),
      status (P (p, "status"), Toggle::Style::Paddle),
      power (P (p, "power"), Toggle::Style::Power)
{
    setSize (kWidth, kHeight);
    for (Component* c : std::initializer_list<Component*> { &programLevel, &carrierLevel, &balance, &mode, &mic, &sampleHold,
                                                            &externalPatch, &hfDirect, &status, &power })
        addAndMakeVisible (c);

    programLevel.setBounds (326, 61, 48, 48);
    carrierLevel.setBounds (326, 346, 48, 48);
    balance.setBounds (681, 351, 48, 48);
    mode.setBounds (838, 353, 44, 44);
    mic.setBounds (147, 62, 26, 48);
    sampleHold.setBounds (799, 62, 36, 50);
    externalPatch.setBounds (987, 62, 36, 50);
    status.setBounds (951, 345, 36, 50);
    hfDirect.setBounds (549, 354, 38, 30);
    power.setBounds (40, 222, 34, 62);
}

Point<float> Panel::jackPos (int row, int index)
{
    return { 165.0f + (float) index * (940.0f / 15.0f), row == 0 ? 186.0f : 268.0f };
}

void Panel::paint (Graphics& g)
{
    const float W = kWidth, H = kHeight;

    // Panel: black anodised aluminium, slight sheen, frame
    g.fillAll (Colour (0xff1c1c1d));
    ColourGradient sheen (Colour (0xff141415), 0, 0, Colour (0xff070708), 0, H, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (4, 4, W - 8, H - 8, 4);
    g.setColour (Colour (0xff3a3a3c));
    g.drawRoundedRectangle (4, 4, W - 8, H - 8, 4, 1.5f);

    // left column
    g.setColour (kInk);
    g.fillRect (110.0f, 10.0f, 2.0f, H - 20);
    text (g, "POWER", 57, 160, 12);
    drawLed (g, { 57, 190 }, Colour (0xff35ff4a), proc.apvts.getRawParameterValue ("power")->load() > 0.5f ? 1.0f : 0.0f);

    // setup button (patch presets, input routing)
    g.setColour (Colour (0xff2a2a2b));
    g.fillRoundedRectangle (setupButton, 3);
    g.setColour (kInkDim);
    g.drawRoundedRectangle (setupButton, 3, 1.0f);
    text (g, "PATCHES", setupButton.getCentreX(), setupButton.getCentreY() - 6, 9.5f, true, kInk, 80);
    text (g, "& SETUP", setupButton.getCentreX(), setupButton.getCentreY() + 7, 9.5f, true, kInk, 80);

    // ---- top row
    text (g, "MIC", 160, 48, 12);
    text (g, "LINE", 160, 126, 12);

    bracket (g, "PROGRAM", 262, 448, 32, 12.5f);
    drawJack (g, { 258, 86 }, false);
    text (g, "INPUT", 258, 117, 11);
    const bool programOnMain = proc.apvts.getRawParameterValue ("routing")->load() > 0.5f;
    const bool sc = proc.lamps.sideChainConnected.load();
    text (g, programOnMain ? "(main input)" : (sc ? "(side-chain)" : "(no side-chain)"), 258, 132, 9, false,
          programOnMain || sc ? kInkDim : Colour (0xffff7a50), 90);
    knobScale (g, { 350, 85 }, 36, { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, 9.5f);
    text (g, "LEVEL", 350, 128, 11);
    text (g, "OVERLOAD", 445, 117, 11);

    // nameplate (generic lettering - this is not the original maker's logo)
    {
        const Rectangle<float> plate (513, 14, 240, 117);
        g.setColour (kInk);
        g.drawRect (plate, 2.5f);
        GlyphArrangement ga;
        const Font f = Font (FontOptions().withHeight (40.0f).withStyle ("Bold"));
        ga.addJustifiedText (f, "SIXTEEN", plate.getX(), plate.getY() + 46, plate.getWidth(), Justification::horizontallyCentred);
        Path letters;
        ga.createPath (letters);
        g.strokePath (letters, PathStrokeType (1.6f));
        text (g, "16 CHANNEL", plate.getCentreX(), 88, 13);
        text (g, "Vocoder", plate.getCentreX(), 113, 22, true, kInk, 240);
    }

    text (g, "SAMPLE / HOLD", 857, 44, 12);
    g.setColour (kInk);
    g.fillRect (835.0f, 86.0f, 70.0f, 2.0f);
    text (g, "EXTERNAL PATCH", 1052, 44, 12);
    g.fillRect (1023.0f, 86.0f, 70.0f, 2.0f);

    // ---- jack field
    text (g, "ANALYZER OUTPUTS", 640, 152, 10);
    text (g, "SYNTHESIZER INPUTS", 640, 302, 10);
    g.setColour (kInk);
    g.fillRect (118.0f, 211.0f, W - 136, 2.0f);
    g.fillRect (118.0f, 241.0f, W - 136, 2.0f);
    for (int i = 0; i <= kNumChannels; ++i)
    {
        const float x = 165.0f + ((float) i - 0.5f) * (940.0f / 15.0f);
        g.fillRect (x - 1, 203.0f, 2.0f, 9.0f);
        g.fillRect (x - 1, 242.0f, 2.0f, 9.0f);
        String label (kPanelEdgesHz[(size_t) i]);
        if (i == kNumChannels) label << " HZ";
        text (g, label, x + (i == kNumChannels ? 10.0f : 0.0f), 227, 11.5f, true, kInk, 70);
    }
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < kNumChannels; ++i)
            drawJack (g, jackPos (row, i), hover.row == row && hover.index == i);

    // ---- bottom row
    bracket (g, "CARRIER", 262, 448, 322, 12.5f);
    drawJack (g, { 258, 370 }, false);
    text (g, "LINE", 258, 401, 10);
    text (g, "INPUT", 258, 414, 10);
    text (g, programOnMain ? (sc ? "(side-chain)" : "(no side-chain)") : "(main input)", 258, 430, 9, false,
          ! programOnMain || sc ? kInkDim : Colour (0xffff7a50), 90);
    knobScale (g, { 350, 370 }, 36, { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, 9.5f);
    text (g, "LEVEL", 350, 414, 11);
    text (g, "OVERLOAD", 445, 401, 11);

    text (g, "5080-15000 HZ", 568, 334, 10);
    text (g, "SWITCHED / DIRECT", 568, 414, 12);

    text (g, "HISS", 662, 333, 10);
    text (g, "BUZZ", 750, 333, 10);
    knobScale (g, { 705, 375 }, 35, { "5", "4", "3", "2", "1", "0", "1", "2", "3", "4", "5" }, 9.5f);
    text (g, "BALANCE", 705, 414, 12);

    text (g, "HISS", 795, 344, 10);
    text (g, "HISS & BUZZ", 860, 328, 10);
    text (g, "BUZZ", 924, 344, 10);
    // position ticks around the selector
    g.setColour (kInk);
    for (int pos = 0; pos < 3; ++pos)
    {
        const float a = ModeSelector::angleFor (pos);
        const Point<float> c (860, 375);
        g.drawLine (c.x + std::sin (a) * 26, c.y - std::cos (a) * 26, c.x + std::sin (a) * 33, c.y - std::cos (a) * 33, 2.0f);
    }
    text (g, "MODE", 860, 414, 12);

    text (g, "VOCODER", 1062, 326, 12);
    g.fillRect (987.0f, 369.0f, 40.0f, 2.0f);
    g.fillRect (1043.0f, 369.0f, 40.0f, 2.0f);
    text (g, "STATUS", 985, 414, 12);
    drawJack (g, { 1100, 370 }, false);
    text (g, "OUTPUT", 1100, 410, 11);

    // ---- lamps
    const bool powered = proc.apvts.getRawParameterValue ("power")->load() > 0.5f;
    auto lamp = [&] (float v) { return powered ? v : 0.0f; };
    const bool hold = proc.apvts.getRawParameterValue ("sampleHold")->load() > 0.5f || proc.lamps.footHold.load();
    drawLed (g, { 440, 86 }, Colour (0xffff2a1a), lamp (lProgOver));
    drawLed (g, { 440, 371 }, Colour (0xffff2a1a), lamp (lCarOver));
    drawLed (g, { 912, 87 }, Colour (0xffffd21a), lamp (hold ? 1.0f : 0.0f));
    drawLed (g, { 1100, 87 }, Colour (0xffffd21a), lamp (proc.apvts.getRawParameterValue ("externalPatch")->load() > 0.5f ? 1.0f : 0.0f));
    drawLed (g, { 791, 374 }, Colour (0xff3aff3a), lamp (lHiss));
    drawLed (g, { 929, 374 }, Colour (0xffff2a1a), lamp (lBuzz));
    drawLed (g, { 1035, 370 }, Colour (0xffffa020), lamp (proc.apvts.getRawParameterValue ("status")->load() > 0.5f ? 1.0f : 0.0f));
}

Colour Panel::cableColour (int source)
{
    static const Colour palette[] = { Colour (0xffd8382e), Colour (0xfff0c419), Colour (0xff2f7fd6), Colour (0xff3fae4a),
                                      Colour (0xffe8792b), Colour (0xffeeeeee), Colour (0xff8a4fc6), Colour (0xff1fb7b0) };
    return palette[source % 8];
}

void Panel::drawCable (Graphics& g, Point<float> a, Point<float> b, Colour col, bool plugA, bool plugB) const
{
    const float dx = std::abs (b.x - a.x);
    const float sag = 6.0f + 0.05f * dx;
    Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.x + (b.x - a.x) * 0.25f, std::max (a.y, b.y) + sag, b.x - (b.x - a.x) * 0.25f, std::max (a.y, b.y) + sag, b.x, b.y);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.strokePath (p, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (2, 4));
    g.setColour (col.darker (0.5f));
    g.strokePath (p, PathStrokeType (6.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (p, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (col.brighter (0.7f).withAlpha (0.6f));
    g.strokePath (p, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (-0.8f, -1.2f));

    auto plug = [&g, col] (Point<float> c)
    {
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (c.x - 10, c.y - 8, 21, 21);
        ColourGradient body (Colour (0xff4a4a4a), c.x - 9, c.y - 9, Colour (0xff0a0a0a), c.x + 9, c.y + 9, false);
        g.setGradientFill (body);
        g.fillEllipse (c.x - 9.5f, c.y - 9.5f, 19, 19);
        g.setColour (col);
        g.drawEllipse (c.x - 9.5f, c.y - 9.5f, 19, 19, 2.0f);
        g.setColour (Colour (0xff8a8a88));
        g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7, 7);
    };
    if (plugA) plug (a);
    if (plugB) plug (b);
}

void Panel::paintOverChildren (Graphics& g)
{
    const bool active = proc.apvts.getRawParameterValue ("externalPatch")->load() > 0.5f;
    for (int j = 0; j < kNumChannels; ++j)
    {
        if (dragging && j == dragFromSynth) continue;
        const int src = proc.getPatch (j);
        if (src < 0) continue;
        auto col = cableColour (src);
        if (! active) col = col.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.7f);
        drawCable (g, jackPos (0, src), jackPos (1, j), col, true, true);
    }
    if (dragging && dragSource >= 0)
        drawCable (g, jackPos (0, dragSource), dragPos, cableColour (dragSource), true, true);

    if (warning.isNotEmpty())
    {
        const Rectangle<float> box (150, 136, 1000, 30);
        g.setColour (Colour (0xe0400c08));
        g.fillRoundedRectangle (box, 4);
        g.setColour (Colour (0xffff8a60));
        g.drawRoundedRectangle (box, 4, 1.5f);
        g.setColour (Colours::white);
        g.setFont (panelFont (11.0f));
        g.drawFittedText (warning, box.reduced (10, 2).toNearestInt(), Justification::centred, 2);
    }
}

Panel::JackHit Panel::hitJack (Point<float> p) const
{
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < kNumChannels; ++i)
            if (p.getDistanceFrom (jackPos (row, i)) < 19.0f)
                return { row, i };
    return {};
}

void Panel::mouseMove (const MouseEvent& e)
{
    const auto h = hitJack (e.position);
    if (h.row != hover.row || h.index != hover.index) { hover = h; repaint (110, 140, kWidth - 110, 180); }
    setMouseCursor (h.valid() || setupButton.contains (e.position) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void Panel::mouseExit (const MouseEvent&)
{
    hover = {};
    repaint (110, 140, kWidth - 110, 180);
}

void Panel::mouseDown (const MouseEvent& e)
{
    if (setupButton.contains (e.position) || e.mods.isPopupMenu())
    {
        showSetupMenu();
        return;
    }
    const auto h = hitJack (e.position);
    if (! h.valid()) return;

    dragFromSynth = -1;
    if (h.row == 0)
        dragSource = h.index;
    else
    {
        const int src = proc.getPatch (h.index);
        if (src >= 0) { dragSource = src; dragFromSynth = h.index; }   // pick up the cord
        else          { dragSource = -1; }
    }
    dragPos = e.position;
    dragging = dragSource >= 0;
    if (dragFromSynth < 0 && h.row == 1)
    {
        // empty synthesizer input: drag towards an analyzer output instead
        dragging = true;
        dragSource = -2 - h.index;   // marker: started at synth input
    }
    repaint();
}

void Panel::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    dragPos = e.position;
    hover = hitJack (e.position);
    repaint();
}

void Panel::mouseUp (const MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    const auto h = hitJack (e.position);

    if (dragSource <= -2)
    {
        const int synth = -2 - dragSource;
        if (h.valid() && h.row == 0) proc.setPatch (synth, h.index);
    }
    else
    {
        if (dragFromSynth >= 0 && ! (h.valid() && h.row == 1 && h.index == dragFromSynth))
            proc.setPatch (dragFromSynth, -1);                    // cord pulled out
        if (h.valid() && h.row == 1)
            proc.setPatch (h.index, dragSource);                  // plugged in
    }
    dragSource = -1;
    dragFromSynth = -1;
    repaint();
}

String Panel::getTooltip()
{
    const auto pos = getMouseXYRelative().toFloat();
    const auto h = hitJack (pos);
    if (h.valid())
    {
        const String band = String (kPanelEdgesHz[(size_t) h.index]) + "-" + String (kPanelEdgesHz[(size_t) h.index + 1]) + " Hz";
        return h.row == 0 ? "Analyzer output " + String (h.index + 1) + " (" + band + "): drag to a synthesizer input to patch"
                          : "Synthesizer input " + String (h.index + 1) + " (" + band + "): drag a cord away to unplug it. "
                            "Cords are used when EXTERNAL PATCH is IN.";
    }
    if (setupButton.contains (pos)) return "Patch presets from the original manuals, input routing and options";
    return {};
}

void Panel::showSetupMenu()
{
    PopupMenu m, presets, routing;
    const auto list = vocoder::patchPresets();
    for (size_t i = 0; i < list.size(); ++i)
        presets.addItem (String (list[i].name) + "  -  " + list[i].description, [this, preset = list[i]]
        {
            proc.applyPatchPreset (preset);
            if (String (preset.name) != "No Cords")
                if (auto* p = proc.apvts.getParameter ("externalPatch"))
                {
                    p->beginChangeGesture(); p->setValueNotifyingHost (1.0f); p->endChangeGesture();
                }
            repaint();
        });
    m.addSubMenu ("Patch cords (sets EXTERNAL PATCH to IN)", presets);

    auto* routingParam = proc.apvts.getParameter ("routing");
    const bool programMain = routingParam->getValue() > 0.5f;
    routing.addItem ("Carrier = main input, Program (voice) = side-chain", true, ! programMain, [routingParam]
                     { routingParam->beginChangeGesture(); routingParam->setValueNotifyingHost (0.0f); routingParam->endChangeGesture(); });
    routing.addItem ("Program (voice) = main input, Carrier = side-chain", true, programMain, [routingParam]
                     { routingParam->beginChangeGesture(); routingParam->setValueNotifyingHost (1.0f); routingParam->endChangeGesture(); });
    m.addSubMenu ("Inputs", routing);

    auto* pre = proc.apvts.getParameter ("preFilter");
    m.addItem ("259 Hz analyzer pre-filter (units above S/N 108)", true, pre->getValue() > 0.5f, [pre]
               { pre->beginChangeGesture(); pre->setValueNotifyingHost (pre->getValue() > 0.5f ? 0.0f : 1.0f); pre->endChangeGesture(); });

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this] (int) { repaint(); });
}

void Panel::updateLamps()
{
    auto take = [] (std::atomic<float>& a) { return a.exchange (0.0f); };
    const float decay = 0.75f;
    // OVERLOAD lamps: full brightness at the overload threshold; below it they glow faintly
    // with the signal level (-40 dB .. 0 dB), so you can see that signal is arriving.
    auto lampLevel = [] (float peak)
    {
        if (peak >= 1.0f) return 1.0f;
        if (peak <= 0.0f) return 0.0f;
        return 0.22f * jlimit (0.0f, 1.0f, (20.0f * std::log10 (peak) + 40.0f) / 40.0f);
    };
    lProgOver = std::max (lampLevel (take (proc.lamps.programPeak)), lProgOver * decay);
    lCarOver  = std::max (lampLevel (take (proc.lamps.carrierPeak)), lCarOver * decay);

    // Diagnose the commonest set-up problem: no voice reaching the program input.
    const float dt = 1.0f / 30.0f;
    const float progIn = take (proc.lamps.programInput), carIn = take (proc.lamps.carrierInput);
    programSilentSeconds = progIn > 1.0e-4f ? 0.0f : programSilentSeconds + dt;
    carrierSeenSeconds   = carIn > 1.0e-4f ? 0.0f : carrierSeenSeconds + dt;
    const bool programOnMain = proc.apvts.getRawParameterValue ("routing")->load() > 0.5f;
    String w;
    if (! programOnMain && ! proc.lamps.sideChainConnected.load())
        w = "NO VOICE: the side-chain input is not active. Click the side-chain button in the plug-in window header, "
            "then send your vocal track to \"Sixteen - Side-Chain\".";
    else if (programSilentSeconds > 1.5f && carrierSeenSeconds < 0.5f)
        w = programOnMain ? "NO VOICE: the main input (program) is silent."
                          : "NO VOICE: the side-chain is active but silent. Check the vocal track's send or routing to "
                            "\"Sixteen - Side-Chain\" (and that the send is on and turned up). A vocoder is silent without a voice.";
    if (w != warning) { warning = w; repaint(); }
    lHiss = std::max (take (proc.lamps.hiss), lHiss * decay);
    lBuzz = std::max (take (proc.lamps.buzz), lBuzz * decay);
    repaint (110, 10, kWidth - 110, 130);   // top row lamps
    repaint (110, 320, kWidth - 110, 130);  // bottom row lamps
    repaint (0, 0, 110, kHeight);           // power, side-chain notes
}

// ---------------------------------------------------------------------------
SixteenEditor::SixteenEditor (SixteenProcessor& p)
    : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) Panel::kWidth / Panel::kHeight);
    setResizeLimits (Panel::kWidth / 2, Panel::kHeight / 2, Panel::kWidth * 2, Panel::kHeight * 2);
    setSize (Panel::kWidth, Panel::kHeight);
    startTimerHz (30);
}

void SixteenEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void SixteenEditor::resized()
{
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) Panel::kWidth));
}

void SixteenEditor::timerCallback() { panel.updateLamps(); }
