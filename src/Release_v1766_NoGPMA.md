# v1.7.66: no bundled Offset Movement Animation

- A user: Oars and Sails overwrote Offset Movement Animation in MO2 and Pandora 5 no longer saw it. The release carried
  the old OAS copy of GP Offset Movement Animation (Nemesis_Engine\mod\gpma, GPMAOffsetAnimation.hkx,
  _1stperson GPMAOffsetAnimation_IC_.hkx). RowRim never uses it.
- release\oas_overlay.zip rebuilt without it (keeps OarsAndSails.esl, the FNIS behaviour, the row clip and its list).
- pack_nexus.bat: README / LICENSE go in the zip as "OarsAndSails README.txt", "OarsAndSails LICENSE.txt"
  (TUNE_SHEET.txt keeps its name) (a user asked: MO2 flagged the plain names as conflicts).
- README: update with Replace and run Nemesis / Pandora once; install Offset Movement Animation on its own if wanted.
- Your own MO2 "Oars and Sails" mod folder still has the old files: delete its Nemesis_Engine folder and the two
  GPMA*.hkx by hand (build.bat does not remove files).
