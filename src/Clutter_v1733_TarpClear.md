# v1.7.33 - tarp clear of the wood, third pass

- The remaining clipping was toward the bow: the clearance only looked at hull points more than 55 out from the centre
  line, and forward of y 200 the gunwale closes in to x 20-55. It now takes the gunwale band wherever it is (within 16
  inside the gunwale line at that y).
- The faces are checked too, not just the corners: 9 points over each cloth quad; where one is within 2.2 of the wood,
  its corners go up (inside the boat) or out (hanging outside) by the overlap, repeated until none is.
- Denser cloth (96 rows along, 18 across, 9 hanging): 10752 verts both faces.
- Check after the build: 430920 points over the cloth, none within 0.8 of the hull, rails or oarlocks.
