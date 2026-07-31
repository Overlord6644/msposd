UAV OSD Mono / UAV OSD Sans Mono
by Nicholas Kruse - https://nicholaskruse.com/work/uavosd

Typeface based on the on-screen display font of the MQ-9 Reaper, reconstructed
by the author from publicly available footage. Two faces: "Mono" keeps the
pixel construction of the original display, "Sans Mono" is the cleaned-up
outline version. Lowercase letters map to the uppercase glyphs, as on the
original display.

License, as stated on the distribution page (dafont.com/uav-osd.font):
  "100% Free" - "Font is free to use for personal or commercial use"

Files here are subsets (tools/subset_font.sh) of the 2020-05-11 release:
  UAV-OSD-Mono-osd.ttf       9792 B  (from UAV-OSD-Mono.ttf, 12636 B)
  UAV-OSD-Sans-Mono-osd.ttf  7068 B  (from UAV-OSD-Sans-Mono.ttf, 9616 B)

Metrics note: this design fills the full em (cap height = em size), unlike a
text face at ~0.7 em. px_text.c normalises for this at load, so a layout `size`
means the same cap height whichever face is installed.
