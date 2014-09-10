// sys includes
#include <math.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <sys/time.h>
#include <cassert>
#include <memory>
#include <numeric>
#include <chrono>
#include <iomanip>
#include <utility>


//
#include <osgnodes.hpp>
#include <projutil.hpp>

double const K_GT_NEPS = -1E-6;
double const K_GT_EPS = 1E-6;

struct STile
{
    // id:
    // z  x      y
    // FF FFFFFF FFFFFF
    // z: tile level (8 bits)
    // x: tile x (24 bits)
    // y: tile y (24 bits)
    uint64_t const id;

    STile * tile_LT;
    STile * tile_LB;
    STile * tile_RB;
    STile * tile_RT;

    STile(uint8_t level,
          uint32_t x,
          uint32_t y) :
        id(GetIdFromLevelXY(level,x,y)),
        tile_LT(nullptr),
        tile_LB(nullptr),
        tile_RB(nullptr),
        tile_RT(nullptr)
    {}

    uint8_t GetLevel() const
    {
        uint64_t level = (id & 0xFF000000000000);
        level = level >> 48;
        return level;
    }

    uint32_t GetX() const
    {
        uint64_t x = (id & 0x00FFFFFF000000);
        x = x >> 24;
        return x;
    }

    uint32_t GetY() const
    {
        return (id & 0x00000000FFFFFF);
    }

    static uint64_t GetIdFromLevelXY(uint8_t level,
                                     uint32_t x,
                                     uint32_t y)
    {
        uint64_t level64 = level;
        uint64_t x64 = x;
        uint64_t y64 = y;
        uint64_t tile_id = 0;
        tile_id |= level64 << 48;
        tile_id |= x64 << 24;
        tile_id |= y64;

        return tile_id;
    }
};

//struct Tile
//{
//    uint64_t id;

//    // id:
//    // z  x      y
//    // FF FFFFFF FFFFFF
//    // z: tile level (8 bits)
//    // x: tile x (24 bits)
//    // y: tile y (24 bits)

//    double min_lon;
//    double max_lon;

//    double min_lat;
//    double max_lat;

//    Tile * tile_LT;
//    Tile * tile_LB;
//    Tile * tile_RB;
//    Tile * tile_RT;

//    Tile() :
//        id(0),
//        tile_LT(nullptr),
//        tile_LB(nullptr),
//        tile_RB(nullptr),
//        tile_RT(nullptr)
////        parent(nullptr)
//    {}

//    Tile(uint64_t id) :
//        id(id),
//        tile_LT(nullptr),
//        tile_LB(nullptr),
//        tile_RB(nullptr),
//        tile_RT(nullptr)
//    {}

//    uint8_t GetLevel() const
//    {
//        uint64_t level = (id & 0xFF000000000000);
//        level = level >> 48;
//        return level;
//    }

//    uint32_t GetX() const
//    {
//        uint64_t x = (id & 0x00FFFFFF000000);
//        x = x >> 24;
//        return x;
//    }

//    uint32_t GetY() const
//    {
//        return (id & 0x00000000FFFFFF);
//    }

//    void SetLevel(uint8_t level)
//    {
//        uint64_t level64 = level;
//        level64 = level64 << 48;

//        id = id & 0x00FFFFFFFFFFFF;
//        id = id | level64;
//    }

//    void SetX(uint32_t x)
//    {
//        uint64_t x64 = x;
//        x64 = x64 << 24;

//        id = id & 0xFF000000FFFFFF;
//        id = id | x64;
//    }

//    void SetY(uint32_t y)
//    {
//        uint64_t y64 = y;

//        id = id & 0xFFFFFFFF000000;
//        id = id | y64;
//    }

//    static uint64_t GetIdFromLevelXY(uint8_t level,
//                                     uint32_t x,
//                                     uint32_t y)
//    {
//        uint64_t level64 = level;
//        uint64_t x64 = x;
//        uint64_t y64 = y;
//        uint64_t tile_id = 0;
//        tile_id |= level64 << 48;
//        tile_id |= x64 << 24;
//        tile_id |= y64;

//        return tile_id;
//    }
//};

bool GeoBoundsIsValid(GeoBounds const &b)
{
    if(b.minLon == 0 && b.maxLon == 0 && b.minLat == 0 && b.maxLat == 0) {
        return false;
    }
    return true;
}

bool GeoBoundsOverlap(GeoBounds const &a,
                      GeoBounds const &b)
{
    if(a.minLon > b.maxLon ||
       a.maxLon < b.minLon ||
       a.minLat > b.maxLat ||
       a.maxLat < b.minLat)
    {   // outside
        return false;
    }
    return true;
}

osg::ref_ptr<osg::Group> BuildTileGeometry(GeoBounds const &root_bounds,
                                           STile const * t)
{
    std::vector<osg::Vec3d> list_vx;
    std::vector<osg::Vec2d> list_tx;
    std::vector<size_t> list_ix;



    double lon_div_degs = (root_bounds.maxLon-root_bounds.minLon)/pow(2,t->GetLevel());
    double lat_div_degs = (root_bounds.maxLat-root_bounds.minLat)/pow(2,t->GetLevel());

    double min_lon = lon_div_degs*t->GetX() + root_bounds.minLon;
    double max_lon = min_lon+lon_div_degs;

    double min_lat = lat_div_degs*t->GetY() + root_bounds.minLat;
    double max_lat = min_lat+lat_div_degs;

//    std::cout << "###: " << int(t->GetLevel()) << " | " << min_lon << "," << max_lon << " | " << min_lat << "," << max_lat << std::endl;

    BuildEarthSurfaceGeometry(min_lon,
                              min_lat,
                              max_lon,
                              max_lat,
                              2 + size_t(floor(pow(2,4-(t->GetLevel())))),
                              2 + size_t(floor(pow(2,4-(t->GetLevel())))),
                              list_vx,
                              list_tx,
                              list_ix);

    osg::ref_ptr<osg::Vec3dArray> vx_array = new osg::Vec3dArray;
    osg::ref_ptr<osg::Vec3Array> nx_array = new osg::Vec3Array;

    for(auto const &vx : list_vx) {
        vx_array->push_back(vx);

        auto nx = vx;
        nx.normalize();
        nx_array->push_back(nx);
    }

    osg::ref_ptr<osg::Vec4Array> cx_array = new osg::Vec4Array;
    for (auto const &tx : list_tx) {
        osg::Vec4 cx = K_COLOR_TABLE[t->GetLevel()];
        double mag = tx.length();
        cx.r() *= mag;
        cx.g() *= mag;
        cx.b() *= mag;
        cx.a() *= mag;
        cx_array->push_back(cx);
    }

    osg::ref_ptr<osg::DrawElementsUShort> ix_array =
            new osg::DrawElementsUShort(GL_TRIANGLES);
    for(auto const ix : list_ix) {
        ix_array->push_back(ix);
    }

    // geometry
    osg::ref_ptr<osg::Geometry> gm = new osg::Geometry;
    gm->setVertexArray(vx_array.get());
    gm->setNormalArray(nx_array.get(),osg::Array::Binding::BIND_PER_VERTEX);
    gm->setColorArray(cx_array,osg::Array::Binding::BIND_PER_VERTEX);
    gm->addPrimitiveSet(ix_array.get());

    // geode
    osg::ref_ptr<osg::Geode> gd = new osg::Geode;
    gd->addDrawable(gm.get());
    gd->getOrCreateStateSet()->setRenderBinDetails(-101 + t->GetLevel(),"RenderBin");
    gd->getOrCreateStateSet()->setMode(
                GL_LIGHTING, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
    gd->getOrCreateStateSet()->setMode(
                GL_CULL_FACE, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);


    // geometry;
    osg::ref_ptr<osg::Group> gp = new osg::Group;
    gp->addChild(gd);

    return gp;
}

void BuildQuadTreeGeometry(osg::ref_ptr<osg::Group> &gp,
                           GeoBounds const &root_bounds,
                           STile * tile)
{
    gp->addChild(BuildTileGeometry(root_bounds,tile));

    if(tile->tile_LT) {
        BuildQuadTreeGeometry(gp,root_bounds,tile->tile_LT);
    }

    if(tile->tile_LB) {
        BuildQuadTreeGeometry(gp,root_bounds,tile->tile_LB);
    }

    if(tile->tile_RB) {
        BuildQuadTreeGeometry(gp,root_bounds,tile->tile_RB);
    }

    if(tile->tile_RT) {
        BuildQuadTreeGeometry(gp,root_bounds,tile->tile_RT);
    }
}

void BuildUpTileToRoot(std::map<uint64_t,std::unique_ptr<STile>> &list_id_tiles,
                       STile * tile)
{
    while(tile->GetLevel() > 0) {
        // Get or create this tile's parent
        STile * parent;
        bool parent_is_new = false;
        uint8_t const parent_level = tile->GetLevel()-1;
        uint32_t const parent_x = tile->GetX()/2;
        uint32_t const parent_y = tile->GetY()/2;
        uint64_t parent_id = STile::GetIdFromLevelXY(parent_level,
                                                    parent_x,
                                                    parent_y);
        auto it = list_id_tiles.find(parent_id);
        if(it == list_id_tiles.end()) {
            it = list_id_tiles.insert(
                        std::pair<uint64_t,std::unique_ptr<STile>>(
                            parent_id,std::unique_ptr<STile>(
                                new STile(parent_level,
                                          parent_x,
                                          parent_y)))).first;
            parent_is_new = true;
        }
        parent = it->second.get();

        // Determine whether this tile is its parent's
        // LT,LB,RB, or RT tile and save a reference
        if(tile->GetX() == parent_x*2) { // left
            if(tile->GetY() == parent_y*2) { // bottom
                parent->tile_LB = tile;
            }
            else { // top
                parent->tile_LT = tile;
            }
        }
        else { // right
            if(tile->GetY() == parent_y*2) { // bottom
                parent->tile_RB = tile;
            }
            else { // top
                parent->tile_RT = tile;
            }
        }

        if(parent_is_new) {
            tile = parent;
        }
        else {
            break;
        }
    }
}

struct vec2
{
    vec2(uint32_t x, uint32_t y) :
        x(x),
        y(y)
    {}

    uint32_t x;
    uint32_t y;
};

void GrowTilesUpToRoot(std::map<uint64_t,std::unique_ptr<STile>> &list_id_tiles,
                       STile * tile)
{
    while(tile->GetLevel() > 0) {
        // Get this this tile's coordinates
        vec2 const xy(tile->GetX(),tile->GetY());

        // Get tiles on the same level but to
        // adjacent in LB,RB,RT,LT
        std::vector<vec2> list_xy;
        list_xy.emplace_back(xy.x-1.0, xy.y-1.0); // LB
        list_xy.emplace_back(xy.x+1.0, xy.y-1.0); // RB
        list_xy.emplace_back(xy.x+1.0, xy.y+1.0); // RT
        list_xy.emplace_back(xy.x-1.0, xy.y+1.0); // LT

        uint8_t const parent_level = tile->GetLevel()-1;
        for(auto const &adj_xy : list_xy)
        {
            // Get or create this adj_xy's parent
            STile * parent;
            uint32_t const parent_x = adj_xy.x/2;
            uint32_t const parent_y = adj_xy.y/2;
            uint64_t parent_id = STile::GetIdFromLevelXY(parent_level,
                                                         parent_x,
                                                         parent_y);
            auto it = list_id_tiles.find(parent_id);
            if(it == list_id_tiles.end()) {
                it = list_id_tiles.insert(
                            std::pair<uint64_t,std::unique_ptr<STile>>(
                                parent_id,std::unique_ptr<STile>(
                                    new STile(parent_level,
                                              parent_x,
                                              parent_y)))).first;
                //parent_is_new = true;
            }
            parent = it->second.get();

            // Check if this parent is @tile's parent as well
            if((xy.x/2 == parent_x) && (xy.y/2 == parent_y))
            {
                // Determine whether this tile is its parent's
                // LT,LB,RB, or RT tile and save a reference
                if(tile->GetX() == parent_x*2) { // left
                    if(tile->GetY() == parent_y*2) { // bottom
                        parent->tile_LB = tile;
                    }
                    else { // top
                        parent->tile_LT = tile;
                    }
                }
                else { // right
                    if(tile->GetY() == parent_y*2) { // bottom
                        parent->tile_RB = tile;
                    }
                    else { // top
                        parent->tile_RT = tile;
                    }
                }

                // The parent is also the next seed tile
                tile = parent;
            }
        }
    }
}

void GenBaseTilesForGeoBounds(GeoBounds const &root_bounds,
                              GeoBounds const &tile_bounds,
                              uint8_t const level,
                              double const lon_div_degs,
                              double const lat_div_degs,
                              std::map<uint64_t,std::unique_ptr<STile>> &list_id_tiles)
{
    // Create bounds that are the intersection
    // of root_bounds and tile_bounds
    if(!GeoBoundsOverlap(root_bounds,tile_bounds)) {
//        std::cout << "#: no overlap, 1: ("
//                  << root_bounds.minLon << "," << root_bounds.maxLon << "),("
//                  << root_bounds.minLat << "," << root_bounds.maxLat << "), 2:("
//                  << tile_bounds.minLon << "," << tile_bounds.maxLon << "),("
//                  << tile_bounds.minLat << "," << tile_bounds.maxLat << ")" << std::endl;
        return;
    }

    GeoBounds xsec_bounds;
    xsec_bounds.minLon = std::max(root_bounds.minLon,tile_bounds.minLon);
    xsec_bounds.maxLon = std::min(root_bounds.maxLon,tile_bounds.maxLon);
    xsec_bounds.minLat = std::max(root_bounds.minLat,tile_bounds.minLat);
    xsec_bounds.maxLat = std::min(root_bounds.maxLat,tile_bounds.maxLat);

    uint32_t const start_x = (xsec_bounds.minLon-root_bounds.minLon)/lon_div_degs;
    uint32_t const end_x = (xsec_bounds.maxLon-root_bounds.minLon)/lon_div_degs;

    uint32_t const start_y = (xsec_bounds.minLat-root_bounds.minLat)/lat_div_degs;
    uint32_t const end_y = (xsec_bounds.maxLat-root_bounds.minLat)/lat_div_degs;

//    std::cout << "###: " << start_x << "," << end_x << "|" << start_y << "," << end_y << std::endl;

    for(uint32_t x = start_x; x <= end_x; x++)
    {
        for(uint32_t y = start_y; y <= end_y; y++)
        {
            uint64_t id = STile::GetIdFromLevelXY(level,x,y);
            auto it = list_id_tiles.find(id);
            if(it == list_id_tiles.end())
            {
                it = list_id_tiles.insert(
                            std::pair<uint64_t,std::unique_ptr<STile>>(
                                id,std::unique_ptr<STile>(new STile(level,x,y)))).first;

//                STile * tile = it->second.get();
//                BuildUpTileToRoot(list_id_tiles,tile);
            }
        }
    }
}




int main()
{
    Frustum frustum;
    Plane horizon_plane;

    // Celestial body geometry
    auto gp_celestial = BuildCelestialSurfaceNode();

    osgViewer::CompositeViewer viewer;
    viewer.setThreadingModel(osgViewer::ViewerBase::SingleThreaded);

    // View0 root
    osg::ref_ptr<osg::Group> gp_root0 = new osg::Group;
    gp_root0->addChild(gp_celestial);

    // View1 root
    osg::ref_ptr<osg::Group> gp_root1 = new osg::Group;
    gp_root1->addChild(gp_celestial);

    // disable lighting and enable blending
    gp_root0->getOrCreateStateSet()->setMode( GL_LIGHTING,osg::StateAttribute::OFF);
    gp_root0->getOrCreateStateSet()->setMode( GL_BLEND,osg::StateAttribute::ON);
    gp_root0->getOrCreateStateSet()->setMode( GL_DEPTH_TEST,osg::StateAttribute::OFF);

    gp_root1->getOrCreateStateSet()->setMode( GL_LIGHTING,osg::StateAttribute::OFF);
    gp_root1->getOrCreateStateSet()->setMode( GL_BLEND,osg::StateAttribute::ON);
    gp_root1->getOrCreateStateSet()->setMode( GL_DEPTH_TEST,osg::StateAttribute::OFF);

    // Create View 0
    {
        osgViewer::View* view = new osgViewer::View;
        viewer.addView( view );

        view->setUpViewInWindow( 10, 10, 640, 480 );
        view->setSceneData( gp_root0.get() );
        view->getCamera()->setClearColor(osg::Vec4(0.1,0.1,0.1,1.0));

        osg::ref_ptr<osgGA::TrackballManipulator> view_manip =
                new osgGA::TrackballManipulator;
        view_manip->setMinimumDistance(100);

        view->setCameraManipulator(view_manip);

    }

    // Create view 1 (this view shows View0's frustum)
    {
        osgViewer::View* view = new osgViewer::View;
        viewer.addView( view );

        view->setUpViewInWindow( 650, 10, 640, 480 );
        view->setSceneData( gp_root1.get() );
        view->getCamera()->setClearColor(osg::Vec4(0.1,0.1,0.1,1.0));

        osg::ref_ptr<osgGA::TrackballManipulator> view_manip =
                new osgGA::TrackballManipulator;
        view_manip->setMinimumDistance(100);

        view->setCameraManipulator(view_manip);
    }

    while(!viewer.done())
    {
        osg::Camera * camera = viewer.getView(0)->getCamera();

        osg::Vec3d eye,vpt,up;
        camera->getViewMatrixAsLookAt(eye,vpt,up);

        PointLLA lla_eye = ConvECEFToLLA(eye);

        // new horizon plane node
        auto new_horizon = BuildHorizonPlaneNode(camera,horizon_plane);
        (void)new_horizon;

        // new camera frustum node
        double far_dist,near_dist;
        if(!CalcCameraNearFarDist(eye,vpt-eye,20000.0,near_dist,far_dist)) {
            far_dist=0.0;
            near_dist=0.0;
        }
        auto new_frustum = BuildFrustumNode(camera,frustum,near_dist,far_dist);


        if(eye.length() <= RAD_AV)
        {
            viewer.frame();
            continue;
        }

        // Get projection center and tangent plane
        osg::Vec3d proj_center = CalcGnomonicProjOrigin(horizon_plane);
        Plane tangent_plane = horizon_plane;
        tangent_plane.p = horizon_plane.n * RAD_AV;
        tangent_plane.d = tangent_plane.n * tangent_plane.p;

        // Get the projection of the camera frustum on
        // the planet surface
        std::vector<osg::Vec3d> poly_frustum_surf;
        CalcProjFrustumPoly(frustum,horizon_plane,poly_frustum_surf);

        // Find the minimum lod that encompasses the
        // frustum proj poly
        size_t min_lod=0;
        size_t max_lod=K_LIST_LOD_DIST.size()-1;
        for(size_t i=0; i < K_LIST_LOD_DIST.size(); i++)
        {
            bool contained = true;
            double dist2 =
                    K_LIST_LOD_DIST[max_lod-i]*
                    K_LIST_LOD_DIST[max_lod-i];

            for(auto const &vx : poly_frustum_surf) {
                if((eye-vx).length2() > dist2) {
                    contained = false;
                    break;
                }
            }

            if(contained) {
                min_lod = max_lod-i;
                break;
            }
        }

        // Create a tile list and add the root tile
        std::map<uint64_t,std::unique_ptr<STile>> list_id_tiles;
        list_id_tiles.insert(std::pair<uint64_t,std::unique_ptr<STile>>(
                                 0,std::unique_ptr<STile>(new STile(0,0,0))));

        STile * root_tile = list_id_tiles.find(0)->second.get();

        GeoBounds root_bounds;
//        root_bounds.minLon = -180.0;
//        root_bounds.maxLon = 180.0;
//        root_bounds.minLat = -90.0;
//        root_bounds.maxLat = 90.0;

        root_bounds.minLon = -180.0;
        root_bounds.maxLon = 0.0;
        root_bounds.minLat = -90.0;
        root_bounds.maxLat = 90.0;

        // Intersect gnomonic projections of distance spheres
        // and the camera frustum and create tiles for the
        // resulting polys
        std::vector<std::vector<osg::Vec3d>> list_polys_xsec(max_lod+1);
        for(size_t i=min_lod; i < max_lod; i++)
        {
            std::vector<osg::Vec3d> poly_lod_sphere;
            CalcProjSpherePoly(horizon_plane,eye,K_LIST_LOD_DIST[i],poly_lod_sphere);
            if(poly_lod_sphere.empty()) {
                break;
            }

            std::vector<osg::Vec3d> poly_xsec;
            std::vector<osg::Vec3d> poly_xsec_tangent;
            CalcGnomonicProjIntersectionPoly(proj_center,
                                             tangent_plane,
                                             poly_frustum_surf,
                                             poly_lod_sphere,
                                             poly_xsec,
                                             poly_xsec_tangent);

            if(!poly_xsec.empty()) {
                list_polys_xsec[i] = poly_xsec; // to render

                std::vector<PointLLA> list_lla;
                list_lla.reserve(poly_xsec.size());
                for(auto const &vx : poly_xsec) {
                    list_lla.push_back(ConvECEFToLLA(vx));
                }
                std::vector<GeoBounds> tempbb;
                CalcMinGeoBoundsFromLLAPoly(lla_eye,list_lla,tempbb);
                if(!tempbb.empty()) {
                    // Create tiles for this geobounds
                    GenBaseTilesForGeoBounds(root_bounds,
                                             tempbb[0],
                                             i,
                                             (root_bounds.maxLon-root_bounds.minLon)/pow(2,i),
                                             (root_bounds.maxLat-root_bounds.minLat)/pow(2,i),
                                             list_id_tiles);
                }
            }
        }

        // List of ordered base tiles (high lod -> low lod)
        size_t const num_base_tiles = list_id_tiles.size();
        std::vector<STile*> list_base_tiles(num_base_tiles);
        {
            size_t ix=1;
            for(auto it = list_id_tiles.begin();
                it != list_id_tiles.end(); ++it)
            {
                list_base_tiles[num_base_tiles-ix] = it->second.get();
                ix++;
            }
        }

        for(STile * tile : list_base_tiles) {
            BuildUpTileToRoot(list_id_tiles,tile);
        }

        // ==================================================== //

        // new lod rings node
        auto new_lodrings = BuildLodRingsNode(eye);

        // new frustumsurfproj
        auto new_frustumsurfpoly =
                BuildSurfacePoly(poly_frustum_surf,
                                 osg::Vec4(0.75,0.5,1.0,1.0),
                                 eye.length()/1200.0);
        new_frustumsurfpoly->setName("frustumsurfpoly");

        // new lodsurfpoly
        osg::ref_ptr<osg::Group> new_lodsurfpoly = new osg::Group;
        for(size_t i=0; i < list_polys_xsec.size(); i++) {
            new_lodsurfpoly->addChild(BuildSurfacePoly(list_polys_xsec[i],
                                                       K_COLOR_TABLE[i],
                                                       eye.length()/1200.0));
        }
        new_lodsurfpoly->setName("lodsurfpoly");

        // new 0_0_pt
        auto new_pt_0_0 = BuildFacingCircle(ConvLLAToECEF(PointLLA(0,0)),
                                            eye.length()/150.0,
                                            8,
                                            osg::Vec4(1,0,0,1));
        new_pt_0_0->setName("pt_0_0");

        // new 90_0_pt
        auto new_pt_90_0 = BuildFacingCircle(ConvLLAToECEF(PointLLA(90,0)),
                                             eye.length()/150.0,
                                             8,
                                             osg::Vec4(0,1,0,1));
        new_pt_90_0->setName("pt_90_0");

        // new vx tiles node
        osg::ref_ptr<osg::Group> new_vxtiles = new osg::Group;
        BuildQuadTreeGeometry(new_vxtiles,root_bounds,root_tile);
        new_vxtiles->setName("vxtiles");


        // Update gp_root0
        {
            for(size_t i=0; i < gp_root0->getNumChildren(); i++) {
                std::string const name = gp_root0->getChild(i)->getName();
                if(name == "frustumsurfpoly") {
                    gp_root0->removeChild(i);
                    i--;
                }
                else if(name == "lodsurfpoly") {
                    gp_root0->removeChild(i);
                    i--;
                }
                else if(name == "tilevx") {
                    gp_root0->removeChild(i);
                    i--;
                }
                else if(name == "pt_0_0") {
                    gp_root0->removeChild(i);
                    i--;
                }
                else if(name =="pt_90_0") {
                    gp_root0->removeChild(i);
                    i--;
                }
                else if(name == "vxtiles") {
                    gp_root0->removeChild(i);
                    i--;
                }
            }
            gp_root0->addChild(new_frustumsurfpoly);
            gp_root0->addChild(new_lodsurfpoly);
            gp_root0->addChild(new_pt_0_0);
            gp_root0->addChild(new_pt_90_0);
            gp_root0->addChild(new_vxtiles);
        }

        // Update gp_root1
        {
            for(size_t i=0; i < gp_root1->getNumChildren(); i++) {
                std::string const name = gp_root1->getChild(i)->getName();
                // Remove prev camera node
                if(name == "frustum") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name == "lodsurfpoly") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name == "lodrings") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name == "frustumsurfpoly") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name == "tilevx") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name == "pt_0_0") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name =="pt_90_0") {
                    gp_root1->removeChild(i);
                    i--;
                }
                else if(name == "vxtiles") {
                    gp_root1->removeChild(i);
                    i--;
                }
            }
            // Add new nodes
            gp_root1->addChild(new_frustum);
            gp_root1->addChild(new_lodrings);
            gp_root1->addChild(new_frustumsurfpoly);
            gp_root1->addChild(new_lodsurfpoly);
            gp_root1->addChild(new_pt_0_0);
            gp_root1->addChild(new_pt_90_0);
            gp_root1->addChild(new_vxtiles);
        }
        viewer.frame();
    }
    return 0;
}


//std::vector<Edge> GetListEdgesForPoly(std::vector<osg::Vec3d> const &poly)
//{
//    std::vector<Edge> list_poly_edges;
//    for(size_t i=1; i < poly.size(); i++) {
//        Edge edge;
//        edge.a = poly[i-1];
//        edge.dirn_ab = poly[i]-edge.a;
//        list_poly_edges.push_back(edge);
//    }
//    // last edge
//    {
//        Edge edge;
//        edge.a = poly[poly.size()-1];
//        edge.dirn_ab = poly[0]-edge.a;
//        list_poly_edges.push_back(edge);
//    }

//    return list_poly_edges;
//}

//bool CalcLonPlaneIntersection(std::vector<Edge> const &list_poly_edges,
//                              osg::Vec3d const &ecef_lon)
//{
//    Plane plane_lon;
//    plane_lon.n = ecef_lon^(osg::Vec3d(0,0,1));
//    plane_lon.p = ecef_lon;
//    plane_lon.d = plane_lon.n*plane_lon.p;

//    for(auto const &edge : list_poly_edges) {
//        // Check if the edge intersects the lon plane
//        double u;
//        auto const xsec_type = CalcLinePlaneIntersection(edge,plane_lon,u);

//        if((xsec_type == XSEC_TRUE && u >= (0-K_GT_EPS) && u <= (1+K_GT_EPS)) ||
//           (xsec_type == XSEC_COINCIDENT))
//        {
//            return true;
//        }
//    }
//    return false;
//}

//bool CalcLatPlaneIntersection(std::vector<Edge> const &list_poly_edges,
//                              osg::Vec3d const &ecef_lat)
//{
//    Plane plane_lat;
//    plane_lat.n = osg::Vec3d(0,0,1);
//    plane_lat.p = ecef_lat;
//    plane_lat.d = plane_lat.n*plane_lat.p;

//    for(auto const &edge : list_poly_edges) {
//        // Check if the edge intersects the lon plane
//        double u;
//        auto const xsec_type = CalcLinePlaneIntersection(edge,plane_lat,u);

//        if((xsec_type == XSEC_TRUE && u >= (0-K_GT_EPS) && u <= (1+K_GT_EPS)) ||
//           (xsec_type == XSEC_COINCIDENT))
//        {
//            return true;
//        }
//    }
//    return false;
//}

//bool CalcTilePolyIntersection(std::vector<Edge> const &list_poly_edges,
//                              VxTile const * tile)
//{
//    // min_lon
//    if(CalcLonPlaneIntersection(list_poly_edges,*(tile->p_ecef_LT)) || // min_lon
//       CalcLonPlaneIntersection(list_poly_edges,*(tile->p_ecef_RT)) || // max_lon
//       CalcLatPlaneIntersection(list_poly_edges,*(tile->p_ecef_LT)) || // min_lat
//       CalcLatPlaneIntersection(list_poly_edges,*(tile->p_ecef_LB)))   // max_lat
//    {
//        return true;
//    }

//    return false;
//}

//// checks whether the center of the tile is contained by the poly
//bool CalcTilePolyContained(std::vector<osg::Vec2d> const &list_proj_poly,
//                           VxTile const * tile,
//                           Plane const &horizon_plane,
//                           Plane const &tangent_plane,
//                           osg::Vec3d const &proj_center)
//{
//    // Ensure that the tile midpoint is above the horizon plane
//    if(CalcPointPlaneSignedDistance(tile->ecef_MM,horizon_plane) < 0) {
//        return false;
//    }

//    // Use gnomonic projection to project the midpoint to proj_plane
//    double u;
//    osg::Vec3d xsec;
//    if(!CalcRayPlaneIntersection(proj_center,
//                                 tile->ecef_MM-proj_center,
//                                 tangent_plane,
//                                 xsec,
//                                 u)) {
//        return false;
//    }

//    // Transform to xy plane
//    osg::Matrixd xf_tangent_to_xy;
//    xf_tangent_to_xy.makeRotate(tangent_plane.n,osg::Vec3d(0,0,1));
//    xsec = xsec * xf_tangent_to_xy;

//    // check mid_lon, mid_lat within list_poly_planes
//    if(CalcPointInPoly(list_proj_poly,osg::Vec2d(xsec.x(),xsec.y()))) {
//        return true;
//    }

//    return false;
//}


