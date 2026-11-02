# Apple Music's play queue

What `AMP.Services.PlayQueue` gives in Apple Music 1.1792, measured in a Windows 11 virtual machine with a list of
songs playing. The interfaces, slots and identifiers are in `src/core/amp_abi.hpp`; the code is in `src/win/amp_player.cpp` and
`src/win/player_bridge.cpp`. The full signatures come from Apple's `.winmd` files run through `cppwinrt.exe` (types and
parameters, which the method lists in `knowledge/winmd/` do not have).

## Reading

- `PlayQueue.Current` (IPlayQueueStatics) answers in `AppleMusic.exe`. `IPlayQueue.get_Sections` is an
  `IObservableVector<IComponentModel>` of `PlayQueueSection` objects: history, current, added by hand, next, autoplay
  (`PlayQueueSectionKind` 1 to 5).
- A section is an `IComponentModelContainer`: `get_Items` lists its entries, `PlayQueueItem` objects. An entry gives
  `PlayQueueItemID` (UInt64, the id `DeletePlayQueueItemsAsync` takes), `Subtitle` ("Artist — Album"), `Duration` (Int64,
  milliseconds), its title through `ITitledComponent` and its catalog id through `IContentDescriptorDriven` then
  `IContentDescriptor.get_AdamId`.
- **The sections stay empty until Apple's queue panel ("Up Next", the list button of the playback bar) has been
  opened once in the session.** Before that, a song can be playing and the queue reads as empty. After it, the queue
  stays filled. Ampify then reports the current track only, with that reason.
- The collections feed the queue panel, so they belong to the interface thread: Ampify reads them there
  (`RunOnUiThreadAndWait`), never from the JavaScript thread.
- Filling the sections without the panel is a dead end so far. `AMP.Services.CommonModels.PlayQueueViewModel`, the
  model behind the panel, is activatable from `AMP.Services.dll` (`DllGetActivationFactory`), but activating one from
  Ampify left every page of Apple Music on its loading spinner until the app was restarted. Ampify does not do it.

## Removing

`IPlayQueue.DeletePlayQueueItemsAsync(IVector<UInt64>)` removes entries; the vector is Ampify's own
(`rt::MakeVector`). Checked on screen: the entry leaves Apple's queue panel at once. Ampify only removes entries still
to come (added, next, autoplay), never the current one or the history.

## Adding: not possible so far

`IPlayer.StartOrQueueLibraryPlaybackAsync(StartOrQueuePlaybackArgs)` looks like the path of the "Play Next" and "Play
Last" entries: `StartOrQueuePlaybackArgs` is activatable, with `Action` (`PlayOrQueueCommand`: StartPlayback 0,
QueueNext 1, QueueLater 2, QueueAfterItem 3, CreateAndPlayStation 4) and `Items` (`IVector<ContentDescriptor>`).

A content descriptor is built from JSON with `IContentDescriptorStatics.DeserializeContentDescriptors`. The format,
as `SerializeContentDescriptors` gives it for a song of the queue:

```json
[{"kind":"song","identifiers":{"storeAdamID":"6826181374"}}]
```

The descriptor read back says it has catalog and cloud identifiers, with the right AdamId. Yet every call completed
without changing anything, from the interface thread and from the JavaScript thread: QueueNext, QueueAfterItem with
`QueueAfterPlayQueueItemID`, QueueNext with `StartItem`, with and without `MetricsJSON`, and even StartPlayback, which
did not start the song. The method most likely serves library playback only; the menus of catalog pages go another
way, not found yet. `Ampify.player.queue.add` therefore answers `false` with that reason.

Also learned: the queue panel's drag data (`IPlayQueueViewModel.SerializeDragData`) is the JSON array of the dragged
entries' ids, such as `[11906]`, under the identifier `PlayQueueViewDragData`. `InsertItemsIntoPlayQueue` takes it, so it
moves entries of the queue; it does not add catalog songs.

## Identifiers of parameterized interfaces

`IVector<T>` and the other generic interfaces have no fixed identifier. It is the UUID version 5 of the interface's
signature in the WinRT namespace `11F47AD5-7B73-42C0-ABAE-878B1E16ADEE`, for example in Python:

```python
uuid.uuid5(uuid.UUID("11f47ad5-7b73-42c0-abae-878b1e16adee"),
           "pinterface({913337e9-11a1-4345-a3a2-4e7f956e222d};{3cf6e1ae-8043-53f2-97bb-a73faec1ac06})")
```

gives `2C559B4A-4165-5E91-86F1-CD3EC0884419`, `IVector<IComponentModel>`, one of the interfaces Apple Music lists for
`PlayQueue.Sections` (with its `IVectorView`, `IIterable` and `IObservableVector`, computed the same way).
