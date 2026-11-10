# Theme effects: what is left, and how it could be done

Colour filters on images (`filter: grayscale()`, `duotone()`...) and a liquid glass backdrop are built; themes in the
mini player are still missing. This note records how the first were done and what the second would take, so the next
attempt starts from the facts.

## Colour filters and liquid glass: how they are built

- One set of effect descriptions for both, in `src/win/image_effects.*`: Direct2D's Gaussian blur, colour matrix and
  composite, written by hand as `IGraphicsEffectD2D1Interop` classes (the Windows App SDK ships none of these and
  Win2D is not loaded in Apple Music). Compositor effect factories accept them.
- `filter` (`src/win/element_blur.*`) keeps the blur's route: the element's single child drawn into a
  `CompositionVisualSurface`, shown from the parent's child visual, the element clipped away. The colour filters fold
  into one 5x4 matrix in the core (`src/core/theme/color_matrix.*`, tested), applied before the blur in the same
  brush: blur and colour matrix commute (the blur's weights sum to 1), so the order written does not change the image.
- Composition animates no 5x4 matrix, and an effect's fixed properties are compiled into its factory. A change of
  colours is therefore a new "look" (a container visual over the surface) faded in over the old one, which goes when
  the fade ends; a blur still eases on its brush. Factories are cached per matrix (64 at most).
- The route over a backdrop brush placed over the image (route 1 of the first study) was not taken: it samples the
  window each frame and feeds back over Apple's own backdrop blur in the full-screen player.
- `backdrop: liquid-glass()` (`src/win/glass_brush.*`) is a `XamlCompositionBrushBase` subclass, made without IDL
  through the projection's `XamlCompositionBrushBaseT`: backdrop brush, saturation matrix, blur, then the tint and a
  sheen gradient composited over. A brush, not a visual, because the panes it targets hold many children. It eases its
  blur and tint in when connected. No refraction: Composition has no displacement effect.
- Still to measure on screen: the cost of a filter on a card artwork recycled by Apple's lists, and liquid glass over
  the moving-artwork background of Glass.

## Clips: why `UIElement.Clip` and not the composition visual

`overflow` and `clip-path` write `UIElement.Clip` (a `RectangleGeometry`, resized on `SizeChanged`). The first version
put an `InsetClip` on the element's composition visual (`ElementCompositionPreview.GetElementVisual`), which follows
the size by itself. It worked on an artwork and failed on cards: WinUI refuses the hand-out visual of an element that
uses the facade properties (`Translation`, `Scale`, `Rotation`), and Apple Music animates many elements that way.
Taking the hand-out visual first would also make Apple's own facade writes on that element throw later. Ampify
therefore never asks for an element's hand-out visual.

## Themes in the mini player

`MiniPlayerWindow` runs on its own interface thread. The snapshot already walks Apple's other windows when they share
the main window's thread (the full-screen player), and skips the others
(`XamlTreeBackend::Snapshot`: `other.DispatcherQueue().HasThreadAccess()`).

What it needs:

- A second `UiSide` (a `XamlTreeBackend` and a `ThemeSession`) bound to the mini player's `DispatcherQueue`, created
  when `AppWindow(L"MiniPlayerWindow")` starts answering and dropped when it stops (`E_BOUNDS` while closed). Every
  XAML object stays on its own thread: the two backends never share a kept object, a decor or a brush.
- The theme service posts each pass to both queues with the same theme; the engine already takes one backend per
  pass. Resources written to `Application.Resources` are shared by both threads' windows: only one side may write
  them (the main one), or the second would see its own writes as changes.
- Stable names for the mini player's parts (`.mini-player`, `.mini-lcd`...), read from the view
  (`knowledge/apple-music-native-views.md` lists them), and the `player` flag of `NodeInfo` so main-window rules do not
  leak into it.
- The `artwork:` brush and `--art-*` colours follow the main window's artwork; the mini player would need its own
  sampler or a value handed across threads as plain colours.

Risks: two passes at once on two threads write the same `settings.txt`-driven state; the service's tick must stay the
only writer. Closing the mini player destroys its tree while a pass may be queued: the side must check the window is
still there at the start of each posted task.

Estimate: two to three days, most of it in the lifetime of the second side and in tests of opening and closing the
mini player while a theme changes.
