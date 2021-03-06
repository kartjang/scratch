#include <TileVisibilityLLPixelsPerMeter.h>
#include <GeometryUtils.h>
#include <OSGUtils.h>

namespace scratch
{
    // TODO move these to GeometryUtils
    static osg::Vec3d CalcPointLineProjection(osg::Vec3d const &pt,
                                              osg::Vec3d const &a,
                                              osg::Vec3d const &b,
                                              bool clamp=false)
    {
        static const double k_eps = 1E-8;

        osg::Vec3d const dirn = b-a;

        double const u_den = dirn*dirn;
        if(fabs(u_den) < k_eps) {
            return a;
        }

        double const u = ((pt*dirn)-(a*dirn))/u_den;

        if(clamp) {
            if(u < 0) {
                return a;
            }
            else if(u > 1) {
                return b;
            }
        }

        return (a+(dirn*u));
    }


    static Circle CalcCircleForLatPlane(Plane const &plane_lat)
    {
        Circle c;
        c.normal = plane_lat.n;
        c.center = CalcPointLineProjection(plane_lat.p,
                                           K_ZERO_VEC,
                                           osg::Vec3d(0,0,1),
                                           false);
        c.radius = (c.center-plane_lat.p).length();
        return c;
    }

    static Circle CalcCircleForLonPlane(Plane const &plane_lon)
    {
        Circle c;
        c.normal = plane_lon.n;
        c.center = K_ZERO_VEC;
        c.radius = RAD_AV;

        return c;
    }

    // ============================================================= //

    TileVisibilityLLPixelsPerMeter::Eval::Eval(TileLL::Id id,
                                               GeoBounds const &bounds) :
        id(id)
    {
        // surface area
        surf_area_m2 = CalcGeoBoundsArea(bounds);

        // (mid lon,mid lat) ecef
        LLA lla_mid;
        lla_mid.lon = (bounds.minLon+bounds.maxLon)*0.5;
        lla_mid.lat = (bounds.minLat+bounds.maxLat)*0.5;
        lla_mid.alt = 0.0;
        ecef_mid = ConvLLAToECEF(lla_mid);

        // corners
        c_min_min = ConvLLAToECEF(LLA(bounds.minLon,bounds.minLat));
        c_max_min = ConvLLAToECEF(LLA(bounds.maxLon,bounds.minLat));
        c_max_max = ConvLLAToECEF(LLA(bounds.maxLon,bounds.maxLat));
        c_min_max = ConvLLAToECEF(LLA(bounds.minLon,bounds.maxLat));

        // lon and lat planes
        plane_min_lon = CalcLonPlane(bounds.minLon,true,true);
        plane_max_lon = CalcLonPlane(bounds.maxLon,false,true);
        plane_min_lat = CalcLatPlane(bounds.minLat,false);
        plane_max_lat = CalcLatPlane(bounds.maxLat,true);

        // circle arc edges
        circle_min_lon = CalcCircleForLonPlane(plane_min_lon);
        circle_max_lon = CalcCircleForLonPlane(plane_max_lon);
        circle_min_lat = CalcCircleForLatPlane(plane_min_lat);
        circle_max_lat = CalcCircleForLatPlane(plane_max_lat);
    }

    // ============================================================= //

    TileVisibilityLLPixelsPerMeter::
    TileVisibilityLLPixelsPerMeter(double view_width_px,
                                   double view_height_px,
                                   size_t texture_px_size,
                                   size_t eval_cache_size) :
        m_view_width(view_width_px),
        m_view_height(view_height_px),
        m_texture_px_size(texture_px_size),
        m_texture_px_area(texture_px_size*texture_px_size),
        m_eval_cache_size(eval_cache_size)
    {

    }

    TileVisibilityLLPixelsPerMeter::
    ~TileVisibilityLLPixelsPerMeter()
    {

    }

    void TileVisibilityLLPixelsPerMeter::
    Update(osg::Camera const * cam)
    {
        // update view data
        m_cam = cam;

        osg::Vec3d eye,vpt,up;
        cam->getViewMatrixAsLookAt(eye,vpt,up);

        double ar,fovy,z_near,z_far;
        cam->getProjectionMatrixAsPerspective(fovy,ar,z_near,z_far);

        //
        m_eye = eye;
        m_lla_eye = ConvECEFToLLA(eye);

        Frustum frustum;
        {   // create the frustum
            auto osg_frustum = BuildFrustumNode(
                        "frustum",cam,frustum,z_near,z_far);
            (void)osg_frustum;
        }

        Plane horizon_plane = CalcHorizonPlane(eye);

        // TODO check calcprojfrustumpoly is successful
        CalcProjFrustumPoly(frustum,horizon_plane,m_list_frustum_ecef);

        m_list_frustum_bounds.clear();
        m_list_frustum_lla = ConvListECEFToLLA(m_list_frustum_ecef);
        CalcMinGeoBoundsFromLLAPoly(ConvECEFToLLA(eye),
                                    m_list_frustum_lla,
                                    m_list_frustum_bounds);

        if(m_list_frustum_bounds.empty()) {
            return;
        }

        m_list_frustum_tri_planes =
                calcFrustumPolyTriPlanes(m_list_frustum_ecef,true);
    }

    void TileVisibilityLLPixelsPerMeter::
    GetVisibility(TileLL const * tile,
                  TileDataSourceLL::Data const * data,
                  bool & is_visible,
                  double & norm_error,
                  osg::Vec3d & closest_point)
    {
        // Ignore extra @data; this implementation of TileVisibility
        // only considers the geometry described in @tile
        (void)data;

        // Get the eval for this tile
        Eval const * eval;
        auto eval_it = m_lru_eval.find(tile->id);

        if(eval_it == m_lru_eval.end()) {
            // Create the evaluation geometry
            std::unique_ptr<Eval> new_eval(
                        new Eval(tile->id,tile->bounds));

            eval_it = m_lru_eval.insert(
                        m_lru_eval.begin(),
                        std::make_pair(
                            tile->id,
                            std::move(new_eval)));

            m_lru_eval.trim(m_eval_cache_size);
        }
        else {
            // reuse
            m_lru_eval.move(eval_it,m_lru_eval.begin());
        }
        eval = eval_it->second.get();

        // Determine if the tile is visible by intersecting
        // it with the projection of the view frustum
        is_visible = calcFrustumTileIntersection(
                    (*eval),
                    m_list_frustum_ecef,
                    m_list_frustum_bounds,
                    m_list_frustum_tri_planes,
                    tile->bounds);

        if(!is_visible) {
            norm_error = -1.0;
            return;
        }


        // Estimate the number of pixels taken up by this
        // tile by using the tile's surface area, and the
        // number of pixels per meter at the closest
        // point on the tile wrt to the camera eye
        closest_point =
                calcTileClosestPoint(m_lla_eye,
                                     m_eye,
                                     tile->bounds,
                                     (*eval));

        double const px_m =
                calcPixelsPerMeterForDist(
                    (m_eye-closest_point).length(),
                    m_view_height,
                    m_cam);

        // Estimated pixel area = (pix/m)*(pix/m)*(m^2)
        double const tile_px_area =
                px_m*px_m*(eval->surf_area_m2);

        // Greater than 1.0 if the current view's tile
        // pixel area exceeds its texture pixel area
        norm_error = tile_px_area/m_texture_px_area;
    }


    bool TileVisibilityLLPixelsPerMeter::
    calcFrustumTileIntersection(Eval const &eval,
                                std::vector<osg::Vec3d> const &list_frustum_vx,
                                std::vector<GeoBounds> const &list_frustum_bounds,
                                std::vector<Plane> const &list_frustum_tri_planes,
                                GeoBounds const &tile_bounds) const
    {
        // TODO determine a good tolerance (this is in meters)
        static const double k_eps = 0.0;

        if(list_frustum_vx.size() != 8) {
            return false;
        }

        // Three tests:
        // 0. GeoBounds intersection
        //  * if the geobounds of the frustum poly and tile
        //    do not intersect, there is no intersection

        bool xsec = false;
        GeoBounds xsec_bounds;
        for(auto const &frustum_bounds : list_frustum_bounds) {
            if(CalcGeoBoundsIntersection(frustum_bounds,
                                         tile_bounds,
                                         xsec_bounds))
            {
                if(xsec_bounds == frustum_bounds) {
                    // Frustum poly is within the tile
    //                std::cout << "#: XSEC TYPE 1" << std::endl;
                    return true;
                }

                xsec = true;
                break;
            }
        }

        if(!xsec) {
            return false;
        }

        // Check to see if the frustum poly edges intersect
        // with any tile planes

        size_t xsec_count;
        std::vector<osg::Vec3d> list_xsec;
        list_xsec.reserve(list_frustum_vx.size());

        // TODO:
        // Would it be faster to compute intersections
        // against all the tile edge planes everytime?

        // min_lon
        {
            xsec_count = CalcPlanePolyIntersection(eval.plane_min_lon,
                                                   list_frustum_vx,
                                                   list_xsec);
            for(size_t i=0; i < xsec_count; i++) {
                if(calcPointWithinTilePlanes(list_xsec[list_xsec.size()-1-i],
                                             eval.plane_min_lon,
                                             eval.plane_max_lon,
                                             eval.plane_min_lat,
                                             eval.plane_max_lat)) {
    //                std::cout << "#: XSEC TYPE 2" << std::endl;
                    return true;
                }
            }
        }

        // max_lon
        {
            xsec_count = CalcPlanePolyIntersection(eval.plane_max_lon,
                                                   list_frustum_vx,
                                                   list_xsec);

            for(size_t i=0; i < xsec_count; i++) {
                if(calcPointWithinTilePlanes(list_xsec[list_xsec.size()-1-i],
                                             eval.plane_min_lon,
                                             eval.plane_max_lon,
                                             eval.plane_min_lat,
                                             eval.plane_max_lat)) {
    //                std::cout << "#: XSEC TYPE 2" << std::endl;
                    return true;
                }
            }
        }

        // max_lat
        {
            xsec_count = CalcPlanePolyIntersection(eval.plane_min_lat,
                                                   list_frustum_vx,
                                                   list_xsec);

            for(size_t i=0; i < xsec_count; i++) {
                if(calcPointWithinTilePlanes(list_xsec[list_xsec.size()-1-i],
                                             eval.plane_min_lon,
                                             eval.plane_max_lon,
                                             eval.plane_min_lat,
                                             eval.plane_max_lat)) {
    //                std::cout << "#: XSEC TYPE 2" << std::endl;
                    return true;
                }
            }
        }

        // min_lat
        {
            xsec_count = CalcPlanePolyIntersection(eval.plane_max_lat,
                                                   list_frustum_vx,
                                                   list_xsec);

            for(size_t i=0; i < xsec_count; i++) {
                if(calcPointWithinTilePlanes(list_xsec[list_xsec.size()-1-i],
                                             eval.plane_min_lon,
                                             eval.plane_max_lon,
                                             eval.plane_min_lat,
                                             eval.plane_max_lat)) {
    //                std::cout << "#: XSEC TYPE 2" << std::endl;
                    return true;
                }
            }
        }

        // Check to see if the frustum poly completely
        // contains the tile (the previous test verified
        // the the tile isn't partially contained) by
        // finding if any of the triangle regions of
        // the frustum poly contains a point from the tile
        {
            // Test point from tile // TODO replace with Eval
            osg::Vec3d const &ecef = eval.ecef_mid;

            for(size_t i=0; i < list_frustum_tri_planes.size(); i+=3) {
                Plane const &plane0 = list_frustum_tri_planes[i+0];
                Plane const &plane1 = list_frustum_tri_planes[i+1];
                Plane const &plane2 = list_frustum_tri_planes[i+2];

                bool outside =
                        ((ecef-plane0.p)*plane0.n > k_eps) ||
                        ((ecef-plane1.p)*plane1.n > k_eps) ||
                        ((ecef-plane2.p)*plane2.n > k_eps);

                if(!outside) {
    //                std::cout << "#: XSEC_TYPE 3_" << i/3 << std::endl;
                    return true;
                }
            }
        }

        return false;
    }

    bool TileVisibilityLLPixelsPerMeter::
    calcPointWithinTilePlanes(osg::Vec3d const &point,
                              Plane const &plane_min_lon,
                              Plane const &plane_max_lon,
                              Plane const &plane_min_lat,
                              Plane const &plane_max_lat) const
    {
        static const double k_eps = 1E-5; // meters

        bool outside =
                (((point-plane_min_lon.p)*plane_min_lon.n) > k_eps) ||
                (((point-plane_max_lon.p)*plane_max_lon.n) > k_eps) ||
                (((point-plane_min_lat.p)*plane_min_lat.n) > k_eps) ||
                (((point-plane_max_lat.p)*plane_max_lat.n) > k_eps);

        return (!outside);
    }

    std::vector<Plane> TileVisibilityLLPixelsPerMeter::
    calcFrustumPolyTriPlanes(std::vector<osg::Vec3d> const &list_frustum_vx,
                             bool normalize) const
    {
        static const std::vector<uint16_t> list_ix = {
            0,1,7,
            1,2,3,
            3,4,5,
            5,6,7,
            5,7,1,
            5,1,3
        };

        std::vector<Plane> list_tri_planes;
        list_tri_planes.reserve(18);

        for(size_t i=0; i < list_ix.size(); i+=3) {
            // Each plane is the plane that goes through
            // the points of a triangle edge, and (0,0,0)
            // since each edge represents a great arc

            osg::Vec3d const &v0 = list_frustum_vx[list_ix[i+0]];
            osg::Vec3d const &v1 = list_frustum_vx[list_ix[i+1]];
            osg::Vec3d const &v2 = list_frustum_vx[list_ix[i+2]];

            Plane plane0;
            plane0.n = (v1-v0)^v0;
            if(normalize) {
                plane0.n.normalize();
            }
            plane0.p = v0;
            plane0.d = plane0.n*plane0.p;

            Plane plane1;
            plane1.n = (v2-v1)^v1;
            if(normalize) {
                plane1.n.normalize();
            }
            plane1.p = v1;
            plane1.d = plane1.n*plane1.p;

            Plane plane2;
            plane2.n = (v0-v2)^v2;
            if(normalize) {
                plane2.n.normalize();
            }
            plane2.p = v2;
            plane2.d = plane2.n*plane2.p;

            list_tri_planes.push_back(plane0);
            list_tri_planes.push_back(plane1);
            list_tri_planes.push_back(plane2);
        }

        return list_tri_planes;
    }

    osg::Vec3d TileVisibilityLLPixelsPerMeter::
    calcTileClosestPoint(LLA const &lla_distal,
                         osg::Vec3d const &ecef_distal,
                         GeoBounds const &bounds,
                         Eval const &eval) const
    {
        // If the LLA of the distal point falls within
        // the bounds, return the corresponding ecef
        if(CalcWithinGeoBounds(bounds,lla_distal)) {
            // TODO CalcRayBodyIntersection might be faster
            LLA lla_closest = lla_distal;
            lla_closest.alt = 0.0;
            return ConvLLAToECEF(lla_closest);
        }

        // Generate a list of possible closest points from
        // tile corner points and tile edges
        std::vector<osg::Vec3d> list_closest;
        list_closest.reserve(8);

        // Add the corner points of the tile
        list_closest.push_back(eval.c_min_min);
        list_closest.push_back(eval.c_max_min);
        list_closest.push_back(eval.c_max_max);
        list_closest.push_back(eval.c_min_max);

        // Add the closest points on the great circle arc
        // edges for each tile (and filter out points that
        // aren't along the edges of the tile)
        std::vector<Circle const *> const list_circles = {
            &eval.circle_min_lon,
            &eval.circle_max_lon,
            &eval.circle_min_lat,
            &eval.circle_max_lat
        };

        for(auto const circle : list_circles) {
            osg::Vec3d closest = ClosestPointCirclePoint(*circle,ecef_distal);
            if(calcPointWithinTilePlanes(closest,
                                         eval.plane_min_lon,
                                         eval.plane_max_lon,
                                         eval.plane_min_lat,
                                         eval.plane_max_lat))
            {
                list_closest.push_back(closest);
            }
        }

        // Calculate all the dist2s for the closest points
        std::vector<double> list_closest_dist2;
        list_closest_dist2.reserve(list_closest.size());
        for(auto const &closest : list_closest) {
            list_closest_dist2.push_back((ecef_distal-closest).length2());
        }

        double abs_closest_dist2 = list_closest_dist2[0];
        osg::Vec3d abs_closest = list_closest[0];

        for(size_t i=1; i < list_closest_dist2.size(); i++) {
            if(list_closest_dist2[i] < abs_closest_dist2) {
                abs_closest_dist2 = list_closest_dist2[i];
                abs_closest = list_closest[i];
            }
        }

        return abs_closest;
    }

    double TileVisibilityLLPixelsPerMeter::
    calcPixelsPerMeterForDist(double dist_m,
                              double screen_height_px,
                              osg::Camera const * cam) const
    {
        double fovy_degs,ar,znear,zfar;
        cam->getProjectionMatrixAsPerspective(fovy_degs,ar,znear,zfar);

        double fovy_rads = fovy_degs * K_PI/180.0;
        double px_per_m = screen_height_px/(2.0*dist_m*tan(fovy_rads*0.5));

        return px_per_m;
    }

}
