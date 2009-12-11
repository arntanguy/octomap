// $Id$

/**
* Octomap:
* A  probabilistic, flexible, and compact 3D mapping library for robotic systems.
* @author K. M. Wurm, A. Hornung, University of Freiburg, Copyright (C) 2009.
* @see http://octomap.sourceforge.net/
* License: GNU GPL v2, http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <math.h>
#include <cassert>

namespace octomap {


  template <class NODE>
  OcTreeBase<NODE>::OcTreeBase(double _resolution) :
    itsRoot(NULL), tree_depth(16), tree_max_val(32768), 
    resolution(_resolution), tree_size(0) {
    
    this->setResolution(_resolution);
    for (unsigned i = 0; i< 3; i++){
      maxValue[i] = -1e6;
      minValue[i] = 1e6;
    }
    sizeChanged = true;
  }


  template <class NODE>
  OcTreeBase<NODE>::~OcTreeBase(){
  }

  template <class NODE>
  void OcTreeBase<NODE>::setResolution(double r) {
    resolution = r;
    resolution_factor = 1. / resolution;
    tree_center(0) = tree_center(1) = tree_center(2) = ((double) tree_max_val) / resolution_factor;
  }


  template <class NODE>
  bool OcTreeBase<NODE>::genKey(double val, unsigned short int& key) const{

    // scale to resolution and shift center for tree_max_val
    int scaled_val =  ((int) floor(resolution_factor * val)) + tree_max_val;

    // key within range of tree?
    if (( scaled_val > 0) && (((unsigned int) scaled_val) < (2*tree_max_val))) {
      key = scaled_val;
      return true;
    }
    else {
      return false;
    }
  }

  template <class NODE>
  bool OcTreeBase<NODE>::genKeys(const point3d& point, unsigned short int (&keys)[3]) const{
    for (unsigned int i=0; i<3; i++) {
      if ( !genKey( point(i), keys[i]) ) {
        return false;
      }
    }

    return true;
  }

  template <class NODE>
  bool OcTreeBase<NODE>::genVal(unsigned short int& key, double& val) const{

    val = 0.0;

    if (key >= 2*tree_max_val)
      return false;

    val = ((double(key) - tree_max_val) + 0.5) * resolution;

    return true;
  }


  template <class NODE>
  unsigned int OcTreeBase<NODE>::genPos(unsigned short int key[], int i) const {

    unsigned int retval = 0;
    if (key[0] & (1 << i)) retval += 1;
    if (key[1] & (1 << i)) retval += 2;
    if (key[2] & (1 << i)) retval += 4;
    return retval;
  }


  template <class NODE>
  NODE* OcTreeBase<NODE>::search(const point3d& value) const {

    // Search is a variant of insert which aborts if
    // it had to insert nodes

    unsigned short int key[3];

    for (unsigned int i=0; i<3; i++) {
      if ( !genKey( value(i), key[i]) ) {
        std::cerr << "Error in OcTreeBase<NODE>: Coordinate "<<i<<" of searched point out of OcTree bounds: "<< value(i)<<"\n";
        return NULL;
      }
    }

    NODE* curNode = itsRoot;

    // follow nodes down to last level...
    for (int i=(tree_depth-1); i>=0; i--) {

      unsigned int pos = genPos(key, i);

      if (curNode->childExists(pos)) {
        //curNode = static_cast<NODE*>( curNode->getChild(pos) );
        // cast removed:
        curNode = curNode->getChild(pos);
      }
      else {
        // we expected a child but did not get it
        // is the current node a leaf already?
        if (!curNode->hasChildren()) {
          return curNode;
        }
        else {
          // it is not, search failed
          return NULL;
        }
      }
    } // end for
    return curNode;
  }

  template <class NODE>
  bool OcTreeBase<NODE>::computeRay(const point3d& origin, const point3d& end, 
			  std::vector<point3d>& _ray) const {

    // see "A Faster Voxel Traversal Algorithm for Ray Tracing" by Amanatides & Woo
    // basically: DDA in 3D

    _ray.clear();

    // Initialization phase -------------------------------------------------------

    point3d direction = (end - origin).unit();
    double maxLength = (end - origin).norm2();

    // Voxel integer coordinates are the indices of the OcTree cells
    // at the lowest level (they may exist or not).

    unsigned short int voxelIdx[3];  // voxel integer coords
    unsigned short int endIdx[3];    // end voxel integer coords
    int step[3];                     // step direction

    double tMax[3];
    double tDelta[3];

    for(unsigned int i=0; i < 3; ++i) {
      if (!genKey(origin(i), voxelIdx[i])) {
        std::cerr << "Error in OcTree::computeRay(): Coordinate "<<i<<" of origin out of OcTree bounds: "<< origin(i)<<"\n";
        return false;
      }
      if (!genKey(end(i), endIdx[i])) {
        std::cerr << "Error in OcTree::computeRay(): Coordinate "<<i<<" of endpoint out of OcTree bounds"<< end(i)<<"\n";
        return false;
      }


      if (direction(i) > 0.0) step[i] =  1;
      else if (direction(i) < 0.0)   step[i] = -1;
      else step[i] = 0;

      double voxelBorder = (double) ( (int) voxelIdx[i] - (int) tree_max_val ) * resolution;
      if (step[i] > 0) voxelBorder += resolution;

      if (direction(i) != 0.0) {
        tMax[i] = ( voxelBorder - origin(i) ) / direction(i);
        tDelta[i] = resolution / fabs( direction(i) );
      }
      else {
        tMax[i] = 1e6;
        tDelta[i] = 1e6;
      }
    }

    // origin and end in same cell: done (ray empty)
    if (voxelIdx[0] == endIdx[0] && voxelIdx[1] == endIdx[1] && voxelIdx[2] == endIdx[2]){
      return true;
    }

    // Incremental phase  ---------------------------------------------------------

    bool done = false;
    while (!done) {
      unsigned int i;

      // find minimum tMax:
      if (tMax[0] < tMax[1]){
        if (tMax[0] < tMax[2]) i = 0;
        else                   i = 2;
      }
      else {
        if (tMax[1] < tMax[2]) i = 1;
        else                   i = 2;
      }

      // advance in direction "i":
      voxelIdx[i] += step[i];

      if ((voxelIdx[i] >= 0) && (voxelIdx[i] < 2*tree_max_val)){
        tMax[i] += tDelta[i];
      }
      else {
        std::cerr << "WARNING: Ray casting in OcTreeBaseNODE>::getCellsOnRay hit the boundary in dim. "<< i << std::endl;
        return false;
      }

      // generate world coords from tree indices
      double val[3];
      for (unsigned int j = 0; j < 3; j++) {
        if(!genVal( voxelIdx[j], val[j] )){
          std::cerr << "Error in OcTree::computeRay(): genVal failed!\n";
          return false;
        }
      }
      point3d value(val[0], val[1], val[2]);

      // reached endpoint?
      if ((value - origin).norm2() > maxLength) {
        done = true;
        break;
      }
      else {
        _ray.push_back(value);
      }
    } // end while

    return true;
  }


  template <class NODE>
  void OcTreeBase<NODE>::getLeafNodes(std::list<OcTreeVolume>& nodes, unsigned int max_depth) const{
    assert(itsRoot);
    if (tree_size <= 1) return; // A tree with only the root is an empty tree (by definition)

    if (max_depth == 0)
      max_depth = tree_depth;

    getLeafNodesRecurs(nodes, max_depth, itsRoot, 0, tree_center);
  }


  template <class NODE>
  void OcTreeBase<NODE>::getLeafNodesRecurs(std::list<OcTreeVolume>& nodes,
            unsigned int max_depth,
            NODE* node, unsigned int depth,
						const point3d& parent_center) const{

    if ((depth <= max_depth) && (node != NULL) ) {

      if (node->hasChildren() && (depth != max_depth)) {

        double center_offset = tree_center(0) / pow( 2., (double) depth+1);
        point3d search_center;

        for (unsigned int i=0; i<8; i++) {
          if (node->childExists(i)) {

            // x-axis
            if (i & 1)	search_center(0) = parent_center(0) + center_offset;
            else  	search_center(0) = parent_center(0) - center_offset;

            // y-axis
            if (i & 2)	search_center(1) = parent_center(1) + center_offset;
            else	search_center(1) = parent_center(1) - center_offset;
            // z-axis
            if (i & 4)	search_center(2) = parent_center(2) + center_offset;
            else	search_center(2) = parent_center(2) - center_offset;

            getLeafNodesRecurs(nodes,max_depth,node->getChild(i), depth+1, search_center);

          } // GetChild
        }
      }
      else {    // node is a leaf node or max depth reached
        double voxelSize = resolution * pow(2., double(tree_depth - depth));
        nodes.push_back(std::make_pair<point3d, double>(parent_center - tree_center, voxelSize));
      }
    }

  }


  template <class NODE>
  void OcTreeBase<NODE>::getVoxels(std::list<OcTreeVolume>& voxels, unsigned int max_depth) const{
    assert(itsRoot);

    if (max_depth == 0)
      max_depth = tree_depth;

    getVoxelsRecurs(voxels,max_depth,itsRoot, 0, tree_center);
  }



  template <class NODE>
  void OcTreeBase<NODE>::getVoxelsRecurs(std::list<OcTreeVolume>& voxels,
      unsigned int max_depth,
      NODE* node, unsigned int depth,
      const point3d& parent_center) const{

    if ((depth <= max_depth) && (node != NULL) ) {
      if (node->hasChildren() && (depth != max_depth)) {

        double center_offset = tree_center(0) / pow(2., (double) depth + 1);
        point3d search_center;

        for (unsigned int i = 0; i < 8; i++) {
          if (node->childExists(i)) {

            // x-axis
            if (i & 1)
              search_center(0) = parent_center(0) + center_offset;
            else
              search_center(0) = parent_center(0) - center_offset;

            // y-axis
            if (i & 2)
              search_center(1) = parent_center(1) + center_offset;
            else
              search_center(1) = parent_center(1) - center_offset;
            // z-axis
            if (i & 4)
              search_center(2) = parent_center(2) + center_offset;
            else
              search_center(2) = parent_center(2) - center_offset;

            getVoxelsRecurs(voxels, max_depth, node->getChild(i), depth + 1, search_center);

          } else{ // GetChild
            double voxelSize = resolution * pow(2., double(tree_depth - depth));
            voxels.push_back(std::make_pair<point3d, double>(parent_center - tree_center, voxelSize));
          }
        } // depth
      }
      // lowest level (= OcTree cells) is not drawn
    }
  }


}