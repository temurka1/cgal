// Copyright (c) 2010-2011  GeometryFactory Sarl (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org).
// You can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $URL$
// $Id$
//
//
// Author(s)     : Laurent Rineau

#ifndef CGAL_TRIANGULATE_POLYHEDRON_H

#include <CGAL/Modifier_base.h>
#include <CGAL/HalfedgeDS_decorator.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/Euler_operations.h>

#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Constrained_triangulation_plus_2.h>
#include <CGAL/Triangulation_2_filtered_projection_traits_3.h>

#include <CGAL/Polygon_mesh_processing/compute_normal.h>

#include <boost/foreach.hpp>

#include <queue>
#include <vector>
#include <utility>

namespace CGAL {

namespace Polygon_mesh_processing {

namespace internal {

template <class PM
          , typename VertexPointMap
          , typename Kernel>
class Triangulate_modifier
  : public CGAL::Modifier_base<PM>
{
  typedef Kernel Traits;

  typedef typename boost::graph_traits<PM>::vertex_descriptor vertex_descriptor;
  typedef typename boost::graph_traits<PM>::halfedge_descriptor halfedge_descriptor;
  typedef typename boost::graph_traits<PM>::face_descriptor face_descriptor;
  typedef typename boost::graph_traits<PM>::edge_descriptor edge_descriptor;
  typedef typename Kernel::Point_3 Point;

  typedef CGAL::Triangulation_2_filtered_projection_traits_3<Traits>   P_traits;

  typedef CGAL::Triangulation_vertex_base_with_info_2<halfedge_descriptor,
                                                      P_traits>        Vb;

  struct Face_info {
    typename boost::graph_traits<PM>::halfedge_descriptor e[3];
    bool is_external;
  };

  typedef CGAL::Triangulation_face_base_with_info_2<Face_info,
                                                    P_traits>          Fb1;
  typedef CGAL::Constrained_triangulation_face_base_2<P_traits, Fb1>   Fb;
  typedef CGAL::Triangulation_data_structure_2<Vb,Fb>                  TDS;
  typedef CGAL::No_intersection_tag                                    Itag;
  typedef CGAL::Constrained_Delaunay_triangulation_2<P_traits,
                                                     TDS,
                                                     Itag>             CDTbase;
  typedef CGAL::Constrained_triangulation_plus_2<CDTbase>              CDT;

  VertexPointMap _vpmap;

public:
  Triangulate_modifier(VertexPointMap vpmap)
    : _vpmap(vpmap)
  {
  }

  bool is_external(typename CDT::Face_handle fh) const {
    return fh->info().is_external;
  }

  void operator()(PM& pmesh)
  {
    // One need to store facet handles into a vector, because the list of
    // facets of the polyhedron will be modified during the loop, and
    // that invalidates the range [facets_begin(), facets_end()[.
    std::vector<face_descriptor> facets;
    facets.reserve(num_faces(pmesh));
    BOOST_FOREACH(face_descriptor fit, faces(pmesh))
      //only consider non-triangular faces
      if ( next( next( halfedge(fit, pmesh), pmesh), pmesh) !=
           prev( halfedge(fit, pmesh), pmesh) ) facets.push_back(fit);

    // Iterates on the vector of facet handles
    for (unsigned int i = 0; i < facets.size(); ++i)
    {
      face_descriptor f = facets[i];
      typename Traits::Vector_3 normal =
        Polygon_mesh_processing::compute_face_normal(f, pmesh);

      P_traits cdt_traits(normal);
      CDT cdt(cdt_traits);

      // Halfedge_around_facet_circulator
      typedef typename CDT::Vertex_handle Tr_Vertex_handle;
      halfedge_descriptor start = halfedge(f, pmesh);
      halfedge_descriptor h = start;
      Tr_Vertex_handle previous, first;
      do
      {
        Tr_Vertex_handle vh = cdt.insert(_vpmap[target(h, pmesh)]);
        if(first == 0) {
          first = vh;
        }
        vh->info() = h;
        if(previous != Tr_Vertex_handle() && previous != vh) {
          cdt.insert_constraint(previous, vh);
        }
        previous = vh;
        h = next(h, pmesh);

      } while( h != start );
      cdt.insert_constraint(previous, first);

      // sets mark is_external
      for(typename CDT::All_faces_iterator fit = cdt.all_faces_begin(),
                                           end = cdt.all_faces_end();
                                           fit != end; ++fit)
      {
        fit->info().is_external = false;
      }
      std::queue<typename CDT::Face_handle> face_queue;
      face_queue.push(cdt.infinite_vertex()->face());
      while(! face_queue.empty() )
      {
        typename CDT::Face_handle fh = face_queue.front();
        face_queue.pop();
        if(fh->info().is_external) continue;
        fh->info().is_external = true;
        for(int i = 0; i <3; ++i) {
          if(!cdt.is_constrained(std::make_pair(fh, i)))
          {
            face_queue.push(fh->neighbor(i));
          }
        }
      }

      // then modify the polyhedron
      // make_hole
      Euler::make_hole(halfedge(f, pmesh), pmesh);

      for(typename CDT::Finite_edges_iterator eit = cdt.finite_edges_begin(),
                                              end = cdt.finite_edges_end();
                                              eit != end; ++eit)
      {
        typename CDT::Face_handle fh = eit->first;
        const int index = eit->second;
        typename CDT::Face_handle opposite_fh = fh->neighbor(eit->second);
        const int opposite_index = opposite_fh->index(fh);
        const Tr_Vertex_handle va = fh->vertex(cdt. cw(index));
        const Tr_Vertex_handle vb = fh->vertex(cdt.ccw(index));

        if( ! (is_external(fh) && is_external(opposite_fh))
          && ! cdt.is_constrained(*eit) ) 
        {
          // strictly internal edge
          edge_descriptor e = add_edge(pmesh);
          fh->info().e[index] = h;
          opposite_fh->info().e[opposite_index] = opposite(h, pmesh);

          set_target(h, target(va->info(), pmesh), pmesh);
          set_target(opposite(h, pmesh), target(vb->info(), pmesh), pmesh);
        }
        if( cdt.is_constrained(*eit) )
        {
          if(!is_external(fh)) {
            fh->info().e[index] = va->info();
          }
          if(!is_external(opposite_fh)) {
            opposite_fh->info().e[opposite_index] = vb->info();
          }
        }
      }
      for(typename CDT::Finite_faces_iterator fit = cdt.finite_faces_begin(),
                                              end = cdt.finite_faces_end();
                                              fit != end; ++fit)
      {
        if(!is_external(fit)) 
        {
          halfedge_descriptor h0 = fit->info().e[0];
          halfedge_descriptor h1 = fit->info().e[1];
          halfedge_descriptor h2 = fit->info().e[2];
          CGAL_assertion(h0 != halfedge_descriptor());
          CGAL_assertion(h1 != halfedge_descriptor());
          CGAL_assertion(h2 != halfedge_descriptor());

          set_next(h0, h1, pmesh);
          set_next(h1, h2, pmesh);
          set_next(h2, h0, pmesh);

          Euler::fill_hole(h0, pmesh);
        }
      }
    } // end loop on facets of the input polyhedron
  }

}; // end class Triangulate_modifier

}//end namespace internal

/**
* \ingroup PkgPolygonMeshProcessing
* Triangulates faces of the polygon mesh `pmesh`. This function depends on the package \ref PkgTriangulation2Summary
* @tparam PolygonMesh a model of `FaceListGraph` and `MutableFaceGraph`
* @tparam VertexPointMap a model of `ReadablePropertyMap` with
    `boost::graph_traits<PolygonMesh>::%vertex_descriptor` as key type and
    `Kernel::Point_3` as value type.
* @tparam Kernel a model of \cgal Kernel.

* @param pmesh the polygon mesh to be triangulated
* @param vpmap the property map with the points associated to the vertices of `pmesh`
* @param k an instance of the kernel
*/
template <typename PolygonMesh
        , typename VertexPointMap
        , typename Kernel
#ifdef DOXYGEN_RUNNING
= CGAL::Kernel_traits< typename  boost::property_traits<VertexPointMap>::value_type >::type
#endif
>
void triangulate_faces(PolygonMesh& pmesh
                     , VertexPointMap vpmap
#ifdef DOXYGEN_RUNNING
                     = get(vertex_point, pmesh)
#endif
                     , const Kernel&
#ifdef DOXYGEN_RUNNING
                     k = Kernel()
#endif
                    )
{
  internal::Triangulate_modifier<PolygonMesh, VertexPointMap, Kernel> modifier(vpmap);
  modifier(pmesh);
}

///\cond SKIP_IN_MANUAL
template <typename PolygonMesh
        , typename VertexPointMap>
void triangulate_faces(PolygonMesh& pmesh
                     , VertexPointMap vpmap)
{
  typedef typename Kernel_traits<
    typename boost::property_traits<
    typename boost::property_map<
    PolygonMesh, CGAL::vertex_point_t>::type>::value_type>::Kernel Kernel;
  return triangulate_faces(pmesh, vpmap, Kernel());
}

template <typename PolygonMesh>
void triangulate_faces(PolygonMesh& pmesh)
{
  return triangulate_faces(pmesh, get(boost::vertex_point, pmesh));
}
///\endcond



} // end namespace Polygon_mesh_processing

} // end namespace CGAL

#endif // CGAL_TRIANGULATE_POLYHEDRON_H
