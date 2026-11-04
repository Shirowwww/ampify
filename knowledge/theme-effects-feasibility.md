# Theme effects: what is left, and how it could be done

Two effects of Spicetify themes are still missing from Ampify's theme engine: a colour filter on an image (duotone
artwork, `filter: grayscale()`), and themes in the mini player. Neither is built; this note records what each would
take, so the next attempt starts from the facts.

## Clips: why `UIElement.Clip` and not the composition visual

`overflow` and `clip-path` write `UIElement.Clip` (a `RectangleGeometry`, resized on `SizeChanged`). The first version
put an `InsetClip` on the element's composition visual (`ElementCompositionPreview.GetElementVisual`), which follows
the size by itself. It worked on an artwork and failed on cards: WinUI refuses the hand-out visual of an element that
uses the facade properties (`Translation`, `Scale`, `Rotation`), and Apple Music animates many elements that way.
Taking the hand-out visual first would also make Apple's own facade writes on that element throw later. Ampify
therefore never asks for an element's hand-out visual.

## Colour filter on an image (duotone, grayscale)

What it needs:

- A `CompositionEffectBrush` with Direct2D's colour matrix (`CLSID_D2D1ColorMatrix`) or its tint and grayscale
  effects. The Windows App SDK ships no effect classes for these (`Microsoft.UI.Composition.Effects` has only lighting),
  and Win2D is not loaded in Apple Music. The effect description must be written by hand: a small WinRT class that
  implements `IGraphicsEffect`, `IGraphicsEffectSource` and `IGraphicsEffectD2D1Interop` (effect CLSID, property
  count, the 5x4 matrix as a property, one named source). Compositor effect factories accept such a class.
- A source for the brush. Two routes:
  1. A `SpriteVisual` placed over the image with `ElementCompositionPreview.SetElementChildVisual`, painted by the
     effect brush whose source is `Compositor.CreateBackdropBrush()`: it filters whatever is drawn under it. Its size
     must follow the element (an `ExpressionAnimation` on the element's size is not available without the hand-out
     visual, so a `SizeChanged` handler). It is not known whether `SetElementChildVisual` conflicts with Apple's
     facade animations the way `GetElementVisual` does: test on a card artwork first.
  2. A `CompositionSurfaceBrush` over a `LoadedImageSurface` of the same image: needs the image's URI, which Apple's
     artworks do not expose (they are `SoftwareBitmapSource` or streams).
- A property, for example `filter: grayscale(1)` or `filter: duotone(#dark, #light)` on `.artwork`, converted into the
  matrix in the core (testable), and written by the backend as the child visual.

Risks: a child visual over every artwork of a page is one more visual per card (hundreds in a long shelf); the backdrop
brush samples the window at each frame. Lesson 5 of `docs/development/xaml-lessons.md` does not apply (no resource is
re-read), but the cost per card must be measured with the "passes after changes" lines of the log before shipping.

Estimate: the interop class and the matrix are a day; the visual's lifetime (cards recycled by Apple's lists, the
element destroyed while the visual is held) is where the time goes.

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
