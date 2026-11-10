#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SkellUi.h"

// Preset browser: search, tag filters, favourites, audition-on-select, randomise within limits, undo / redo.
class PresetBrowser : public juce::Component, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    explicit PresetBrowser (DelayAudioProcessor& p) : proc (p)
    {
        search.setTextToShowWhenEmpty ("search name or tag", SkellUi::dimText());
        search.setFont (juce::FontOptions (11.0f));
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff050c03));
        search.setColour (juce::TextEditor::textColourId, SkellUi::lime());
        search.setColour (juce::TextEditor::outlineColourId, SkellUi::lime().withAlpha (0.35f));
        search.setColour (juce::TextEditor::focusedOutlineColourId, SkellUi::lime());
        search.setColour (juce::CaretComponent::caretColourId, SkellUi::lime());
        search.setColour (juce::TextEditor::highlightColourId, SkellUi::lime().withAlpha (0.35f));
        search.addListener (this);
        search.setTitle ("Search presets");
        addAndMakeVisible (search);

        static const char* chipNames[kChips] = { "ALL", "FAV", "VOCAL", "RHYTHM", "SPACE", "TAPE", "LO-FI", "CREATIVE", "USER" };
        static const char* chipKeys[kChips]  = { "", "*fav", "vocal", "rhythmic", "space", "tape", "lo-fi", "creative", "user" };
        for (int i = 0; i < kChips; ++i)
        {
            chipKey[i] = chipKeys[i];
            chip[i].setButtonText (chipNames[i]);
            chip[i].setClickingTogglesState (true);
            chip[i].setRadioGroupId (771);
            chip[i].getProperties().set ("flat", true);
            chip[i].getProperties().set ("led", true);
            chip[i].onClick = [this, i] { activeChip = i; applyFilter(); };
            chip[i].setTitle (juce::String ("Filter ") + chipNames[i]);
            if (i == 0 || i == 1 || i == kChips - 1) addAndMakeVisible (chip[i]);   // ALL, FAV, USER; category chips stay as hidden filters
        }
        chip[0].setToggleState (true, juce::dontSendNotification);

        list.setModel (this);
        list.setRowHeight (18);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff040a03));
        list.setColour (juce::ListBox::outlineColourId, SkellUi::lime().withAlpha (0.3f));
        list.setOutlineThickness (1);
        list.setTitle ("Presets");
        addAndMakeVisible (list);

        for (auto* b : { &saveBtn, &delBtn, &closeBtn })
        {
            b->getProperties().set ("flat", true);
            addAndMakeVisible (b);
        }
        keepTime.setClickingTogglesState (true);  keepMix.setClickingTogglesState (true);
        keepTime.getProperties().set ("led", true);  keepMix.getProperties().set ("led", true);
        randBtn.onClick  = [this] { proc.randomise (static_cast<float> (amount.getValue()), keepTime.getToggleState(), keepMix.getToggleState()); changed(); };
        saveBtn.onClick  = [this] { if (onSave) onSave(); };
        delBtn.onClick   = [this] { const int r = list.getSelectedRow(); if (juce::isPositiveAndBelow (r, (int) shown.size()) && shown[(size_t) r].user && onDelete) onDelete (shown[(size_t) r].file); };
        closeBtn.onClick = [this] { if (onClose) onClose(); };
        undoBtn.onClick  = [this] { proc.undoManager.undo(); changed(); };
        redoBtn.onClick  = [this] { proc.undoManager.redo(); changed(); };
        randBtn.setTooltip ("Randomise: new settings drawn from safe ranges (feedback never runs away). The slider sets how far from the current sound it goes");
        keepTime.setTooltip ("Keep the delay time when randomising");
        keepMix.setTooltip ("Keep the wet level when randomising");
        undoBtn.setTooltip ("Undo the last change (Ctrl/Cmd+Z)");
        redoBtn.setTooltip ("Redo (Ctrl/Cmd+Shift+Z)");

        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        amount.setRange (0.0, 1.0, 0.01);
        amount.setValue (0.6, juce::dontSendNotification);
        amount.setColour (juce::Slider::trackColourId, SkellUi::lime());
        amount.setColour (juce::Slider::thumbColourId, SkellUi::lime());
        amount.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff102408));
        amount.setTitle ("Randomise amount");
        amount.onValueChange = [this] { repaint(); };

        setWantsKeyboardFocus (true);
        setTitle ("Preset browser");
        refresh();
    }

    std::function<void()> onClose, onSave, onChanged;
    std::function<void (juce::File)> onDelete;

    void refresh()
    {
        all = proc.getPresetEntries();
        applyFilter();
        undoBtn.setEnabled (proc.undoManager.canUndo());
        redoBtn.setEnabled (proc.undoManager.canRedo());
        delBtn.setEnabled (juce::isPositiveAndBelow (list.getSelectedRow(), (int) shown.size()) && shown[(size_t) list.getSelectedRow()].user);
    }

    void updateButtons()
    {
        undoBtn.setEnabled (proc.undoManager.canUndo());
        redoBtn.setEnabled (proc.undoManager.canRedo());
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff050c03));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (SkellUi::lime().withAlpha (SkellUi::highContrast ? 0.9f : 0.35f));
        g.drawRoundedRectangle (b, 6.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (22);
        search.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (6);
        const int cw = 64;
        for (int i : { 0, 1, kChips - 1 }) chip[i].setBounds (top.removeFromLeft (cw));
        r.removeFromTop (6);
        auto right = r.removeFromRight (196);
        r.removeFromRight (8);
        list.setBounds (r);

        auto row = right.removeFromTop (22);
        saveBtn.setBounds (row.removeFromLeft (92));   delBtn.setBounds (row.removeFromRight (92));
        right.removeFromTop (8);
        closeBtn.setBounds (right.removeFromTop (22));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        return false;
    }

    int getNumShown() const { return (int) shown.size(); }
    void setSearchText (const juce::String& t) { search.setText (t, true); }
    void selectChip (int i) { chip[i].setToggleState (true, juce::sendNotification); }

private:
    static constexpr int kChips = 9;

    void changed() { refresh(); if (onChanged) onChanged(); repaint(); }

    void textEditorTextChanged (juce::TextEditor&) override { applyFilter(); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { if (onClose) onClose(); }

    void applyFilter()
    {
        const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
        shown.clear();
        for (const auto& e : all)
        {
            const auto hay = (e.name + " " + e.tags).toLowerCase();
            bool ok = true;
            for (const auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) ok = false;
            const juce::String key = chipKey[activeChip];
            if (key == "*fav") ok = ok && DelayAudioProcessor::isFavourite (e.key);
            else if (key.isNotEmpty()) ok = ok && (" " + e.tags + " ").contains (" " + key + " ");
            if (ok) shown.push_back (e);
        }
        const auto cur = proc.getCurrentPresetName();
        syncing = true;
        list.updateContent();
        int sel = -1;
        for (size_t i = 0; i < shown.size(); ++i) if (shown[i].name == cur) sel = (int) i;
        list.selectRow (sel);
        if (sel >= 0) list.scrollToEnsureRowIsOnscreen (sel);
        syncing = false;
        list.repaint();
    }

    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& e = shown[(size_t) row];
        if (selected) { g.setColour (SkellUi::lime().withAlpha (0.22f)); g.fillRect (0, 0, w, h); }
        const bool fav = DelayAudioProcessor::isFavourite (e.key);
        g.setFont (juce::FontOptions (SkellUi::fit (12.0f)).withStyle ("Bold"));
        g.setColour (fav ? juce::Colour (0xffffd24a) : SkellUi::dimText());
        g.drawText (fav ? juce::String::charToString (0x2605) : juce::String::charToString (0x2606), 0, 0, 22, h, juce::Justification::centred, false);
        g.setFont (juce::FontOptions (SkellUi::fit (11.0f)).withStyle (selected ? "Bold" : "Regular"));
        g.setColour (selected ? juce::Colours::white : SkellUi::lime());
        g.drawText (e.name, 24, 0, w - 24 - 120, h, juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (SkellUi::fit (9.0f)));
        g.setColour (SkellUi::dimText());
        g.drawText (e.tags, w - 122, 0, 116, h, juce::Justification::centredRight, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (juce::isPositiveAndBelow (row, (int) shown.size()) && e.x < 22)
        {
            DelayAudioProcessor::setFavourite (shown[(size_t) row].key, ! DelayAudioProcessor::isFavourite (shown[(size_t) row].key));
            if (chipKey[activeChip] == "*fav") applyFilter();
            list.repaint();
        }
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { if (onClose) onClose(); }
    void returnKeyPressed (int) override { if (onClose) onClose(); }
    void deleteKeyPressed (int row) override
    {
        if (juce::isPositiveAndBelow (row, (int) shown.size()))
        {
            DelayAudioProcessor::setFavourite (shown[(size_t) row].key, ! DelayAudioProcessor::isFavourite (shown[(size_t) row].key));
            if (chipKey[activeChip] == "*fav") applyFilter();
            list.repaint();
        }
    }

    void selectedRowsChanged (int row) override
    {
        if (syncing || ! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        proc.newUndoStep ("Preset");
        proc.loadPresetEntry (shown[(size_t) row]);      // audition: the sound changes as the selection moves
        undoBtn.setEnabled (proc.undoManager.canUndo());
        redoBtn.setEnabled (proc.undoManager.canRedo());
        delBtn.setEnabled (shown[(size_t) row].user);
        if (onChanged) onChanged();
    }

    DelayAudioProcessor& proc;
    juce::TextEditor search;
    juce::TextButton chip[kChips];
    juce::String chipKey[kChips];
    int activeChip = 0;
    juce::ListBox list;
    juce::TextButton randBtn { "RANDOMISE" }, saveBtn { "SAVE AS..." }, delBtn { "DELETE" }, closeBtn { "CLOSE" },
                     undoBtn { "UNDO" }, redoBtn { "REDO" }, keepTime { "KEEP TIME" }, keepMix { "KEEP MIX" };
    juce::Slider amount;
    juce::Rectangle<int> amountLabel;
    std::vector<DelayAudioProcessor::PresetEntry> all, shown;
    bool syncing = false;
};
