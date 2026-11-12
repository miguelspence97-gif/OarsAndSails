import pymeshlab, sys
T={'MSBHull':14000,'MSBSailMain':2000,'MSBSailJib':900,'MSBRope01':8000,'MSBRope02':1500,'MSBBlanket':1500,'MSBSack':1500,'MSBOar01':600,'MSBOar02':600}
for n,t in T.items():
    ms=pymeshlab.MeshSet(); ms.load_new_mesh(f'parts/{n}.obj')
    ms.meshing_merge_close_vertices(threshold=pymeshlab.PercentageValue(0.0001))
    ms.meshing_decimation_quadric_edge_collapse_with_texture(targetfacenum=t, qualitythr=0.4, preserveboundary=True, boundaryweight=2.0, optimalplacement=True, preservenormal=True, planarquadric=True)
    m=ms.current_mesh(); print(n, m.face_number(), m.vertex_number())
    ms.save_current_mesh(f'dec/{n}.obj', save_wedge_texcoord=True, save_textures=False)
