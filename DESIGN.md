---
name: HySW
description: A local instrument for exact protein search that shows where its parallel speed comes from.
colors:
  ground: "#f6f6f3"
  surface: "#fcfcfb"
  surface-sunk: "#f0efea"
  rule: "#e1e0d9"
  rule-strong: "#c3c2b7"
  ink: "#0b0b0b"
  ink-secondary: "#52514e"
  ink-muted: "#6b6963"
  signal-blue: "#2a78d6"
  signal-blue-ink: "#ffffff"
  signal-blue-wash: "#e3eefb"
  e-core-orange: "#eb6834"
  ok-green: "#1a8f63"
  warn-wash: "#fff6df"
  warn-line: "#eda100"
  error-wash: "#fdecea"
  error-line: "#c8402f"
  aln-identical-wash: "#d6e7fa"
  aln-identical-ink: "#0d3f78"
  aln-similar-wash: "#d7f2e6"
  aln-similar-ink: "#0c5a3c"
  aln-gap-ink: "#a3a199"
  struct-rest: "#dcdad2"
  struct-identical: "#2a78d6"
  struct-similar: "#1baf7a"
  struct-different: "#8a857b"
  struct-marker: "#eb6834"
  plddt-very-high: "#0053d6"
  plddt-confident: "#65cbf3"
  plddt-low: "#ffdb13"
  plddt-very-low: "#ff7d45"
  selection: "rgb(42 120 214 / 0.22)"
typography:
  display:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "1.6rem"
    fontWeight: 700
    lineHeight: 1.5
    letterSpacing: "-0.01em"
  figure:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "2.1rem"
    fontWeight: 750
    lineHeight: 1.15
    fontFeature: "tnum"
  headline:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "1.15rem"
    fontWeight: 700
    lineHeight: 1.5
  title:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "1rem"
    fontWeight: 700
    lineHeight: 1.5
    letterSpacing: "0.01em"
  body:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "15px"
    fontWeight: 400
    lineHeight: 1.5
  hint:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "0.85rem"
    fontWeight: 400
    lineHeight: 1.5
  label:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu, sans-serif"
    fontSize: "0.76rem"
    fontWeight: 600
    letterSpacing: "0.04em"
  mono:
    fontFamily: "ui-monospace, SFMono-Regular, JetBrains Mono, Menlo, Consolas, monospace"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.5
rounded:
  lane: "2px"
  bar: "3px"
  tag: "4px"
  inner: "6px"
  control: "8px"
  card: "10px"
  pill: "999px"
spacing:
  xs: "0.35rem"
  sm: "0.5rem"
  md: "0.75rem"
  lg: "1rem"
  xl: "1.25rem"
  gutter: "clamp(1rem, 3vw, 2rem)"
components:
  button-primary:
    backgroundColor: "{colors.signal-blue}"
    textColor: "{colors.signal-blue-ink}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "0.5rem 1.1rem"
  button-ghost:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "0.5rem 1.1rem"
  card:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.card}"
    padding: "1.1rem 1.2rem"
  card-pair:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.card}"
    padding: "1.3rem 1.4rem 1rem"
  nav-track:
    backgroundColor: "{colors.surface-sunk}"
    rounded: "9px"
    padding: "3px"
  nav-tab:
    textColor: "{colors.ink-secondary}"
    rounded: "7px"
    padding: "0.35rem 0.9rem"
  nav-tab-current:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
  segmented:
    backgroundColor: "{colors.surface-sunk}"
    rounded: "{rounded.control}"
    padding: "2px"
  segmented-pressed:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.inner}"
  pair-pill:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink-secondary}"
    rounded: "{rounded.pill}"
    padding: "0.4rem 0.95rem"
  pair-pill-selected:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.ground}"
  chip:
    backgroundColor: "{colors.surface-sunk}"
    textColor: "{colors.ink-secondary}"
    rounded: "{rounded.pill}"
    padding: "0.15rem 0.6rem"
  tag-live:
    textColor: "{colors.signal-blue}"
    typography: "{typography.label}"
    rounded: "{rounded.tag}"
    padding: "0 0.45rem"
  tag-stored:
    textColor: "{colors.ink-secondary}"
    rounded: "{rounded.tag}"
  tag-replay:
    textColor: "{colors.ink-muted}"
    rounded: "{rounded.tag}"
  stat-tile:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.card}"
    padding: "0.75rem 0.9rem"
  core-tile:
    backgroundColor: "{colors.surface-sunk}"
    textColor: "{colors.ink-secondary}"
    rounded: "{rounded.control}"
    height: "64px"
    padding: "4px 5px"
  input:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "0.5rem 0.6rem"
---

# Design System: HySW

## Overview

**Creative North Star: "The Bench Instrument"**

HySW looks like a well-made piece of lab equipment sitting on a pale bench: an off-white ground, near-white panels separated by hairline rules, black ink, and colour that appears only where it carries a measurement. Everything on screen is a reading. Numbers are set in tabular figures, every figure says whether it came from a live engine run, a stored benchmark or a replay, and the instruments (core tiles, lane strips, timelines, bars, the protein map) are drawn from the engine's own output rather than decorated.

The colour vocabulary is fixed by the benchmark figures it shares with: blue is P-core work and the gain an "after" side wins, orange is E-core work, and everything else is neutral. Biology gets its own, separate vocabularies: alignment classes in pale washes behind monospaced residues, and the four AlphaFold confidence bands exactly as AlphaFold publishes them. Density is moderate: one card per task, generous line length limits, a single 1240px column.

Three views share one world. Search is the working tool (query card, engine activity card, results table, alignment and structure side by side). The Lab arranges each speedup as a controlled pair: before on the left, after on the right, the change named in a column between them. Setup is plain cards and definition lists.

**Key Characteristics:**
- Warm off-white ground with near-white cards on 1px warm-grey rules; dark theme mirrors every token.
- One blue that means P-core, gain and primary action; one orange that means E-core and the pointed-at residue.
- Tabular numerals everywhere a number can change.
- Provenance tags (LIVE, STORED, REPLAY) on every figure that could be mistaken for another kind.
- Instruments built from thin bars and tiles; the 16-lane strip is the signature mark, echoed by the brand mark and icon.
- Flat surfaces with a whisper of shadow in light mode and none in dark.

## Colors

A warm-neutral bench palette with two signal hues taken from the benchmark figures, plus two fixed scientific vocabularies (alignment classes and AlphaFold confidence).

### Primary
- **Signal Blue** (`--accent`, `--p`; dark `#5b9be6`): the one accent. It fills the primary button, the progress bar, the lane-strip bars, the score bars in the hit table, the highlighted horizontal bar, the "N× faster" gain on an after side, the change column's arrow and label, the LIVE tag, links, focus outlines and the caret. Inside core tiles and timelines it means a P-core thread.
- **Signal Blue Ink** (`--accent-ink`; dark `#0b0b0b`): text on a Signal Blue fill.
- **Signal Blue Wash** (`--accent-soft`; dark `#1d2b3d`): the selected hit row and the info message background.

### Secondary
- **E-core Orange** (`--e`; dark `#f08a5d`): E-core threads in core tiles, timelines and their legend keys. The same hue, as **Structure Marker** (`--st-mark`), marks the residue being pointed at: the outline on an alignment residue, the cursor on the protein map and the sphere in the 3D model.

### Tertiary
Status colours, used only for messages and badges:
- **OK Green** (`--ok`; dark `#3fbf8c`): the "available" badge on databases and the check marks in verification lists.
- **Warn Wash / Warn Line** (`--warn-bg`, `--warn-line`; dark `#2d2614` / `#c98a00`): the warnings message.
- **Error Wash / Error Line** (`--err-bg`, `--err-line`; dark `#331c19` / `#e0725f`): the form error message.

Alignment classes (residue washes in the monospaced alignment):
- **Identical Wash / Ink** (`--id-bg`, `--id-ink`; dark `#1f3a5c` / `#cfe3fb`): identical residues.
- **Similar Wash / Ink** (`--pos-bg`, `--pos-ink`; dark `#183b2d` / `#c4ecd9`): similar (positive-scoring) residues.
- **Gap Ink** (`--gap-ink`; dark `#6d6b65`): gap dashes. Different residues carry no wash at all.

Structure and protein-map classes (`--st-*`): **Structure Rest** (outside the alignment; dark `#45443f`), **Structure Identical** (the Signal Blue; dark `#5b9be6`), **Structure Similar** (a brighter green, dark `#3fbf8c`), **Structure Different** (warm grey, dark `#a7a197`). They are deliberately stronger than the alignment washes because they colour a 3D ribbon and 12px map tracks, not text backgrounds.

AlphaFold confidence (`--af-*`): **Very High** deep blue, **Confident** sky blue, **Low** yellow, **Very Low** orange. These are AlphaFold DB's own published band colours and are identical in both themes.

### Neutral
- **Bench Ground** (`--bg`; dark `#121211`): the page.
- **Panel** (`--surface`; dark `#1b1b1a`): cards, inputs, the current tab, pressed segment, sticky table header.
- **Sunk Panel** (`--surface-2`; dark `#242422`): tracks behind every bar, the nav and segmented-control wells, idle core tiles, chips, row hover, the 3D viewer well.
- **Rule** (`--line`; dark `#2f2f2d`): card borders, table rows, section dividers inside a card.
- **Strong Rule** (`--line-2`; dark `#45443f`): input, chip, pill and tile borders; chart axes.
- **Ink** (`--ink`; dark `#f1f0ec`): headings, values, selected pill fill.
- **Secondary Ink** (`--ink-2`; dark `#bebdb6`): body sentences, sub-lines, legends, STORED tag, the neutral change column.
- **Muted Ink** (`--muted`; dark `#8f8d86`): hints, captions, column headers, axis text, REPLAY tag. It measures 5.1:1 on the ground, 5.4:1 on a panel and 4.8:1 on a sunk panel in light mode (4.7 to 5.6:1 in dark), so it meets WCAG AA for small text; still, it is for secondary text and never for anything the reader must act on.

### Named Rules
**The Benchmark Palette Rule.** Blue is P-core and orange is E-core, everywhere a thread or core is drawn, matching the benchmark figures. No other hue may stand for a core type, and these two may not be reused for unrelated categories inside a core or thread instrument.

**The Gain Is Blue Rule.** On a before/after pair the after side's gain and the change column are Signal Blue; when the change is not an improvement (a loss, or a pair where nothing in the code changes) both fall back to Secondary Ink. A loss is never painted red.

**The Borrowed Vocabulary Rule.** The AlphaFold confidence bands use AlphaFold's own colours and thresholds (90, 70, 50) unchanged in both themes, so a viewer who knows AlphaFold reads them instantly.

## Typography

**Display Font:** the UI sans stack (system-ui, -apple-system, Segoe UI, Roboto, Noto Sans, Ubuntu)
**Body Font:** the same stack, 15px with 1.5 line height
**Label/Mono Font:** ui-monospace stack (SFMono-Regular, JetBrains Mono, Menlo, Consolas) for sequences, accessions and the query box

**Character:** a plain, neutral instrument face whose job is to stay out of the way of the numbers. Weight, not size, does most of the ranking: 600 to 750 for labels and figures, 400 for prose.

### Hierarchy
- **Display** (700, 1.6rem, tight -0.01em tracking): the Lab title only.
- **Figure** (750, 2.1rem, 1.15, tabular): the large time or rate on each side of a pair; its gain suffix drops to 1rem at 650. Summary stat values use the same treatment at 1.45rem, 700.
- **Headline** (700, 1.15rem): the pair title and the Results heading.
- **Title** (650 to 700, 1rem): card headings (700), side labels and pair-extra headings (650); card sub-heads at 0.9rem.
- **Body** (400, 15px, 1.5): sentences. Lead and sub-lines are capped at 68 to 80ch; side sentences at 46ch.
- **Hint** (400, 0.8 to 0.85rem, Muted Ink): helper text, captions, provenance lines (0.8rem, 1.6 line height).
- **Label** (600, 0.76rem, 0.04em, uppercase): table column headers and stat-tile keys only. Provenance tags use the same uppercase treatment at 0.7rem, 650.
- **Mono** (400, 13px, 1.5): the alignment block and the query textarea; accessions in the hit table at 0.82rem.

### Named Rules
**The Tabular Rule.** Every number that can change (progress, timings, counts, scores, axis ticks, CPU labels) uses tabular numerals so live updates never jitter.

**The One Residue Per Cell Rule.** In the alignment block every residue occupies exactly one character cell, so the query, the match line and the hit stay in register and a residue's wash or outline never shifts its neighbours.

## Layout

A single centred column, 1240px maximum, with a fluid side gutter of `clamp(1rem, 3vw, 2rem)` shared by the header, main and footer. The header is a full-width panel strip with the brand at left and the view switcher at right.

Grids carry the views: Search's top row splits 1.25fr / 1fr (query card, engine activity card); the hit detail splits 1.45fr / a 320px-minimum column (alignment, structure); Setup is two equal columns with a full-width calibration card. The Lab pair is a three-column grid, `1fr 7.5rem 1fr`, whose two sides use subgrid rows so label, figure, sentence and instrument line up across before and after. Stat tiles auto-fit at 170px minimum; core tiles auto-fill at 52px minimum (six fixed columns in the Lab's small variant).

Spacing is rem-based and moderate: 0.35rem inside tight clusters, 0.5 to 0.75rem between related items, 1rem between cards, 1.25 to 1.5rem between sections. Cards pad 1.1rem by 1.2rem; the Lab pair card pads a little more (1.3rem by 1.4rem).

Breakpoints: at 1080px the hit detail stacks; at 900px the pair stacks into one column and its change column turns horizontal with the arrow rotated to point down, and the replay and two-column extras stack; at 860px the Search top row and Setup grid stack, the brand tagline hides and the organism column hides; at 520px paired form fields stack and two more table columns hide. Tables scroll horizontally inside their card rather than the page.

## Elevation & Depth

Nearly flat. Depth comes from tone (Panel on Bench Ground, Sunk Panel wells inside panels) and from 1px rules. Light mode adds one barely-there shadow to cards, the current tab and the pressed segment; dark mode removes it entirely and relies on tone alone.

### Shadow Vocabulary
- **Panel whisper** (`box-shadow: 0 1px 2px rgb(0 0 0 / 0.05), 0 2px 8px rgb(0 0 0 / 0.04)`): cards, the current nav tab, the pressed segmented button. Light mode only.
- **Busy ring** (`box-shadow: inset 0 0 0 2px <core colour>`): a core tile whose thread is working. A state, not elevation.

### Named Rules
**The Tone Not Shadow Rule.** Separation is done with Sunk Panel wells and rules. The single shadow never grows on hover and never appears in dark mode.

## Shapes

Gently rounded, with radius scaled to the size of the thing: 10px cards and stat tiles, 8px controls, inputs, core tiles and the viewer well, 6 to 7px buttons inside a well, 4px tags, 3px bars and map tracks, 2px lane bars and residue washes. Choices that are one of several (example chips, pair pills, badges) are full pills. Absent things are dashed: an unused core tile, the legend key for "not used", an empty confidence track.

The signature form is the thin vertical bar: the 16 lane bars of the SIMD strip, the brand mark of 16 bars in four heights, and the five-bar app icon on a Signal Blue tile.

## Components

### Buttons
Quiet and solid: one filled action per region, everything else outlined.
- **Shape:** 8px radius, `0.5rem 1.1rem` padding, 0.9rem text.
- **Primary:** Signal Blue fill and border, Signal Blue Ink text, 600 weight, at least 7rem wide (Search, Run live / Run again). Disabled at 55% opacity with a progress cursor while work runs.
- **Ghost:** Panel fill, Strong Rule border, Ink text (Cancel, Calibrate, export links, replay controls). Hover turns the border Signal Blue.
- **Focus:** 2px Signal Blue outline, 1px offset, on every control including table rows.

### Chips
- **Style:** example-protein chips are Sunk Panel pills with a Strong Rule border and Secondary Ink text at 0.8rem; hover brings a Signal Blue border and Ink text.
- **Badges:** database availability badges are outline pills in OK Green ("available") or Muted Ink, with the word inside.

### Cards / Containers
- **Corner Style:** 10px.
- **Background:** Panel on Bench Ground.
- **Shadow Strategy:** Panel whisper in light mode, none in dark.
- **Border:** 1px Rule.
- **Internal Padding:** 1.1rem by 1.2rem; table cards have no padding so the table runs edge to edge. Sections inside a card are divided by a 1px Rule and 1.1rem of space, never by another card.

### Inputs / Fields
- **Style:** Panel fill, 1px Strong Rule, 8px radius; labels above at 600, 0.9rem, with an optional hint aligned right on the same baseline. The query box is monospaced 13px and resizes vertically.
- **Focus:** 2px Signal Blue outline; caret in Signal Blue.
- **Error:** a message block below the form, Error Wash with an Error Line border, announced as an alert.

### Navigation
The view switcher is a segmented well: Sunk Panel track with 3px padding and 9px radius; tabs in Secondary Ink at 0.9rem; the current view lifts onto Panel with Ink text, 600 weight and the panel whisper. The same pattern at a smaller size is the **segmented control** that switches the structure colouring (Alignment / Confidence), pressed state announced with `aria-pressed`.

### Pair Index
The Lab's comparison picker: a wrapping row of outline pills (Panel fill, Strong Rule border, Secondary Ink, 0.88rem). Hover darkens the border and text; the selected pair inverts to an Ink fill with Bench Ground text at 600. It is a tablist; the pair card below is its tabpanel.

### Before / After Pair
The signature component. One card holds a header row (headline, a one-sentence "The only change:" sub-line, and the primary action at right), then the duo: before side, change column, after side. Each side reads top to bottom as label (Title), big figure (Figure) with an optional gain suffix, one sentence, its instrument, and under the after side a "Stored:" line and, when live and stored disagree by more than 1.5×, a muted "why they differ" note. The change column carries a stretched SVG arrow and the change's short name in Signal Blue at 0.78rem, 650 (Secondary Ink when neutral). A provenance line of tagged sentences sits under the duo, and optional extras (stored charts, replay) follow below a Rule.

### Provenance Tags
Small uppercase outline tags (0.7rem, 650, 0.04em, 4px radius, border in the text colour): **LIVE** in Signal Blue, **STORED** in Secondary Ink, **REPLAY** in Muted Ink. They lead each sentence of a provenance line, so the source of a number is stated in words as well as colour.

### Core Tiles
One tile per logical CPU, 64px tall (46px in the Lab), 8px radius, Sunk Panel with a Strong Rule border. The top line names the CPU and its type letter (P or E); the bottom line counts batches. A fill rises from the bottom in the core's colour at 28% opacity to show that thread's share of the work; a working thread gets the 2px inset busy ring. Unused CPUs are dashed, transparent and dimmed to 60%. A legend with square keys and words always accompanies the grid.

### Lane Strip
Sixteen 6px-wide bars, 56px tall, 7px apart, each a Sunk Panel track with an inset Rule outline. Active lanes fill upward in Signal Blue from the engine's real progress; idle lanes stay empty. One active lane for scalar, sixteen for SIMD, with a muted caption stating the count in words.

### Timelines
Per-core rows for the schedule pair: a CPU label column (4.2rem, tabular) and a 12px Sunk Panel track holding segments in P blue or E orange, with a dashed Secondary Ink tick where that core finished and a muted time axis below.

### Horizontal Bars
Label, 12px track, value: a three-column grid for stored comparisons. Bars are Signal Blue; rows that are not the point of the comparison drop to 45% opacity, and the value is always printed beside the bar.

### Hit Table and Score Cells
Full-width table inside an unpadded card; uppercase Label headers that stick on scroll; 1px Rule between rows; Sunk Panel on hover; Signal Blue Wash when selected. The score column pairs a thin 6px Signal Blue bar with the number.

### Alignment Block
Monospaced blocks of 60 residues: muted "Query" / "Hit" labels and right-aligned positions, residues one per cell with Identical or Similar washes, plain text for different, Gap Ink dashes for gaps. Pointing at a residue outlines it in the Structure Marker colour and finds it on the 3D model and the protein map. A key with swatches and words sits in the card head.

### Protein Map
Two labelled 12px tracks across the whole hit protein (Alignment classes, AlphaFold Confidence), a shared orange cursor that follows the pointer, a tabular position axis with ticks at the aligned region's ends, and a worded key. An empty confidence track is dashed and says what will fill it.

### Structure Viewer
A square Sunk Panel well (max 460px) holding the 3D model, with a centred state message while loading or when unavailable, and a small Panel tooltip at bottom-left naming the hovered residue. A worded legend follows the colouring mode.

### Stat Tiles
Standalone Panel tiles in an auto-fit grid above the results: an uppercase Label key, a 1.45rem tabular value, a Secondary Ink sub-line.

### Messages
Full-width blocks with 8px radius and a 1px coloured border on a wash: error (default), warning, info.

### States
- **Loading:** a 6px sweeping gradient bar (1.2s) in Rule tones; the primary button disables.
- **Progress:** 10px pill progress bar and 4px mini-progress bars in Signal Blue, driven by engine reports with short linear transitions.
- **Empty:** dashed tracks and tiles with a muted sentence; the pair's "pending" text in Muted Ink.
- **Reduced motion:** the progress bar, core fills, lane-strip fills, per-side progress rules and the loading sweep are all switched off.

## Do's and Don'ts

### Do:
- **Do** label every number on screen as live, stored (with its date and settings) or replay/estimate, using the provenance tags or the words themselves.
- **Do** keep blue for P-core threads and orange for E-core threads in every core, thread or timeline instrument, with a P or E letter or a worded legend beside them.
- **Do** pair every colour key with words: alignment classes, structure modes, confidence bands and core types all carry text labels.
- **Do** set every changing number in tabular figures.
- **Do** separate sections inside a card with a 1px Rule and space.
- **Do** show a comparison as a before/after pair on the same work, with the change named between the sides.
- **Do** mirror every new colour token in the dark theme.

### Don't:
- **Don't** nest a card inside a card; inner groupings use rules, wells or instrument tiles, never another bordered, shadowed panel.
- **Don't** let colour be the only carrier of meaning.
- **Don't** draw an instrument from anything but real engine output or a stored result; an animation that is not data is labelled as an illustration.
- **Don't** introduce a new hue for a core type, a gain or a loss; losses use Secondary Ink, not red.
- **Don't** restyle the AlphaFold confidence colours or thresholds.
- **Don't** use Muted Ink for text the reader must act on.
- **Don't** add shadows in dark mode or grow shadows on hover.
