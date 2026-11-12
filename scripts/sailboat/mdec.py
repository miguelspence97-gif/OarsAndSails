import pymeshlab
T={'BigWoodenBoat03':9000,'BigWoodenBoat02':5000,'BigWoodenBoat01':4500,'RustMetal01':2000,'Rope02':5000,'Rope01':5000,'RicePaper01':1500,'BoatFabric01':1500,'BoatFabric02_Main':2000,'BoatFabric02_Jib':900,'BoatFabric02_Rest':16}
for n,t in T.items():
    ms=pymeshlab.MeshSet(); ms.load_new_mesh(f'mparts/{n}.obj')
    if ms.current_mesh().face_number()>t:
        ms.meshing_decimation_quadric_edge_collapse_with_texture(targetfacenum=t, qualitythr=0.4, preserveboundary=True, boundaryweight=2.0, optimalplacement=True, preservenormal=True, planarquadric=True)
    m=ms.current_mesh(); print(n, m.face_number())
    ms.save_current_mesh(f'mdec/{n}.obj', save_wedge_texcoord=True, save_textures=False)
