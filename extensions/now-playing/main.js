// Now Playing: a button of the playback bar that opens Apple Music's own full-screen player, laid over the window
// (Ampify.ui.applePlayer). The lyrics view Ampify drew before is the lyrics-view example.

Ampify.ui.playbar.addButton({
  label: "Now Playing",
  icon: "",  // FullScreen, in Segoe Fluent Icons
  onClick: () => Ampify.ui.applePlayer.open(),
});
