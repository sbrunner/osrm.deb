/*
    open source routing machine
    Copyright (C) Dennis Luxen, others 2010

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU AFFERO General Public License as published by
the Free Software Foundation; either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
or see http://www.gnu.org/licenses/agpl.txt.
 */

#ifndef STATICRTREE_H_
#define STATICRTREE_H_

#include "MercatorUtil.h"
#include "TimingUtil.h"
#include "Coordinate.h"
#include "PhantomNodes.h"
#include "DeallocatingVector.h"
#include "HilbertValue.h"
#include "../typedefs.h"

#include <boost/assert.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/minmax.hpp>
#include <boost/algorithm/minmax_element.hpp>
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>

#include <cassert>
#include <cfloat>
#include <climits>

#include <algorithm>
#include <fstream>
#include <queue>
#include <vector>

//tuning parameters
const static uint32_t RTREE_BRANCHING_FACTOR = 50;
const static uint32_t RTREE_LEAF_NODE_SIZE = 1170;

// Implements a static, i.e. packed, R-tree

static boost::thread_specific_ptr<std::ifstream> thread_local_rtree_stream;

template<class DataT>
class StaticRTree : boost::noncopyable {
private:
    struct RectangleInt2D {
        RectangleInt2D() :
            min_lon(INT_MAX),
            max_lon(INT_MIN),
            min_lat(INT_MAX),
            max_lat(INT_MIN) {}

        int32_t min_lon, max_lon;
        int32_t min_lat, max_lat;

        inline void InitializeMBRectangle(
                const DataT * objects,
                const uint32_t element_count
        ) {
            for(uint32_t i = 0; i < element_count; ++i) {
                min_lon = std::min(
                        min_lon, std::min(objects[i].lon1, objects[i].lon2)
                );
                max_lon = std::max(
                        max_lon, std::max(objects[i].lon1, objects[i].lon2)
                );

                min_lat = std::min(
                        min_lat, std::min(objects[i].lat1, objects[i].lat2)
                );
                max_lat = std::max(
                        max_lat, std::max(objects[i].lat1, objects[i].lat2)
                );
            }
        }

        inline void AugmentMBRectangle(const RectangleInt2D & other) {
            min_lon = std::min(min_lon, other.min_lon);
            max_lon = std::max(max_lon, other.max_lon);
            min_lat = std::min(min_lat, other.min_lat);
            max_lat = std::max(max_lat, other.max_lat);
        }

        inline _Coordinate Centroid() const {
            _Coordinate centroid;
            //The coordinates of the midpoints are given by:
            //x = (x1 + x2) /2 and y = (y1 + y2) /2.
            centroid.lon = (min_lon + max_lon)/2;
            centroid.lat = (min_lat + max_lat)/2;
            return centroid;
        }

        inline bool Intersects(const RectangleInt2D & other) const {
            _Coordinate upper_left (other.max_lat, other.min_lon);
            _Coordinate upper_right(other.max_lat, other.max_lon);
            _Coordinate lower_right(other.min_lat, other.max_lon);
            _Coordinate lower_left (other.min_lat, other.min_lon);

            return (
                    Contains(upper_left)
                    || Contains(upper_right)
                    || Contains(lower_right)
                    || Contains(lower_left)
            );
        }

        inline double GetMinDist(const _Coordinate & location) const {
            bool is_contained = Contains(location);
            if (is_contained) {
                return 0.0;
            }

            double min_dist = DBL_MAX;
            min_dist = std::min(
                    min_dist,
                    ApproximateDistance(
                            location.lat,
                            location.lon,
                            max_lat,
                            min_lon
                    )
            );
            min_dist = std::min(
                    min_dist,
                    ApproximateDistance(
                            location.lat,
                            location.lon,
                            max_lat,
                            max_lon
                    )
            );
            min_dist = std::min(
                    min_dist,
                    ApproximateDistance(
                            location.lat,
                            location.lon,
                            min_lat,
                            max_lon
                    )
            );
            min_dist = std::min(
                    min_dist,
                    ApproximateDistance(
                            location.lat,
                            location.lon,
                            min_lat,
                            min_lon
                    )
            );
            return min_dist;
        }

        inline double GetMinMaxDist(const _Coordinate & location) const {
            double min_max_dist = DBL_MAX;
            //Get minmax distance to each of the four sides
            _Coordinate upper_left (max_lat, min_lon);
            _Coordinate upper_right(max_lat, max_lon);
            _Coordinate lower_right(min_lat, max_lon);
            _Coordinate lower_left (min_lat, min_lon);

            min_max_dist = std::min(
                    min_max_dist,
                    std::max(
                            ApproximateDistance(location, upper_left ),
                            ApproximateDistance(location, upper_right)
                    )
            );

            min_max_dist = std::min(
                    min_max_dist,
                    std::max(
                            ApproximateDistance(location, upper_right),
                            ApproximateDistance(location, lower_right)
                    )
            );

            min_max_dist = std::min(
                    min_max_dist,
                    std::max(
                            ApproximateDistance(location, lower_right),
                            ApproximateDistance(location, lower_left )
                    )
            );

            min_max_dist = std::min(
                    min_max_dist,
                    std::max(
                            ApproximateDistance(location, lower_left ),
                            ApproximateDistance(location, upper_left )
                    )
            );
            return min_max_dist;
        }

        inline bool Contains(const _Coordinate & location) const {
            bool lats_contained =
                    (location.lat > min_lat) && (location.lat < max_lat);
            bool lons_contained =
                    (location.lon > min_lon) && (location.lon < max_lon);
            return lats_contained && lons_contained;
        }

        inline friend std::ostream & operator<< ( std::ostream & out, const RectangleInt2D & rect ) {
            out << rect.min_lat/100000. << "," << rect.min_lon/100000. << " " << rect.max_lat/100000. << "," << rect.max_lon/100000.;
            return out;
        }
    };

    typedef RectangleInt2D RectangleT;

    struct WrappedInputElement {
        explicit WrappedInputElement(const uint32_t _array_index, const uint64_t _hilbert_value) :
			        m_array_index(_array_index), m_hilbert_value(_hilbert_value) {}
        WrappedInputElement() : m_array_index(UINT_MAX), m_hilbert_value(0) {}

        uint32_t m_array_index;
        uint64_t m_hilbert_value;

        inline bool operator<(const WrappedInputElement & other) const {
            return m_hilbert_value < other.m_hilbert_value;
        }
    };

    struct LeafNode {
        LeafNode() : object_count(0) {}
        uint32_t object_count;
        DataT objects[RTREE_LEAF_NODE_SIZE];
    };

    struct TreeNode {
        TreeNode() : child_count(0), child_is_on_disk(false) {}
        RectangleT minimum_bounding_rectangle;
        uint32_t child_count:31;
        bool child_is_on_disk:1;
        uint32_t children[RTREE_BRANCHING_FACTOR];
    };

    struct QueryCandidate {
        explicit QueryCandidate(const uint32_t n_id, const double dist) : node_id(n_id), min_dist(dist)/*, minmax_dist(DBL_MAX)*/ {}
        QueryCandidate() : node_id(UINT_MAX), min_dist(DBL_MAX)/*, minmax_dist(DBL_MAX)*/ {}
        uint32_t node_id;
        double min_dist;
        //		double minmax_dist;
        inline bool operator<(const QueryCandidate & other) const {
            return min_dist < other.min_dist;
        }
    };

    std::vector<TreeNode> m_search_tree;
    uint64_t m_element_count;

    std::string m_leaf_node_filename;
public:
    //Construct a pack R-Tree from the input-list with Kamel-Faloutsos algorithm [1]
    explicit StaticRTree(std::vector<DataT> & input_data_vector, const char * tree_node_filename, const char * leaf_node_filename) :
    m_leaf_node_filename(leaf_node_filename) {
        m_element_count = input_data_vector.size();
        //remove elements that are flagged to be ignored
//        boost::remove_erase_if(input_data_vector, boost::bind(&DataT::isIgnored, _1 ));

        INFO("constructing r-tree of " << m_element_count << " elements");
//        INFO("sizeof(LeafNode)=" << sizeof(LeafNode));
//        INFO("sizeof(TreeNode)=" << sizeof(TreeNode));
//        INFO("sizeof(WrappedInputElement)=" << sizeof(WrappedInputElement));
        double time1 = get_timestamp();
        std::vector<WrappedInputElement> input_wrapper_vector(input_data_vector.size());

        //generate auxiliary vector of hilbert-values
#pragma omp parallel for schedule(guided)
        for(uint64_t element_counter = 0; element_counter < m_element_count; ++element_counter) {
            //INFO("ID: " << input_data_vector[element_counter].id);
            input_wrapper_vector[element_counter].m_array_index = element_counter;
            //Get Hilbert-Value for centroid in mercartor projection
            DataT & current_element = input_data_vector[element_counter];
            _Coordinate current_centroid = current_element.Centroid();
            current_centroid.lat = 100000*lat2y(current_centroid.lat/100000.);

            uint64_t current_hilbert_value = HilbertCode::GetHilbertNumberForCoordinate(current_centroid);
            input_wrapper_vector[element_counter].m_hilbert_value = current_hilbert_value;

        }
        //INFO("finished wrapper setup");

        //open leaf file
        std::ofstream leaf_node_file(leaf_node_filename, std::ios::binary);
        leaf_node_file.write((char*) &m_element_count, sizeof(uint64_t));

        //sort the hilbert-value representatives
        std::sort(input_wrapper_vector.begin(), input_wrapper_vector.end());
        //		INFO("finished sorting");
        std::vector<TreeNode> tree_nodes_in_level;

        //pack M elements into leaf node and write to leaf file
        uint64_t processed_objects_count = 0;
        while(processed_objects_count < m_element_count) {

            LeafNode current_leaf;
            TreeNode current_node;
            for(uint32_t current_element_index = 0; RTREE_LEAF_NODE_SIZE > current_element_index; ++current_element_index) {
                if(m_element_count > (processed_objects_count + current_element_index)) {
                    //					INFO("Checking element " << (processed_objects_count + current_element_index));
                    uint32_t index_of_next_object = input_wrapper_vector[processed_objects_count + current_element_index].m_array_index;
                    current_leaf.objects[current_element_index] = input_data_vector[index_of_next_object];
                    ++current_leaf.object_count;
                }
            }

            if(0 == processed_objects_count) {
                for(uint32_t i = 0; i < current_leaf.object_count; ++i) {
                    					//INFO("[" << i << "] id: " << current_leaf.objects[i].id << ", weight: " << current_leaf.objects[i].weight << ", " << current_leaf.objects[i].lat1/100000. << "," << current_leaf.objects[i].lon1/100000. << ";" << current_leaf.objects[i].lat2/100000. << "," << current_leaf.objects[i].lon2/100000.);
                }
            }

            //generate tree node that resemble the objects in leaf and store it for next level
            current_node.minimum_bounding_rectangle.InitializeMBRectangle(current_leaf.objects, current_leaf.object_count);
            current_node.child_is_on_disk = true;
            current_node.children[0] = tree_nodes_in_level.size();
            tree_nodes_in_level.push_back(current_node);

            //write leaf_node to leaf node file
            leaf_node_file.write((char*)&current_leaf, sizeof(current_leaf));
            processed_objects_count += current_leaf.object_count;
        }

        //		INFO("wrote " << processed_objects_count << " leaf objects");

        //close leaf file
        leaf_node_file.close();

        uint32_t processing_level = 0;
        while(1 < tree_nodes_in_level.size()) {
            //			INFO("processing " << (uint32_t)tree_nodes_in_level.size() << " tree nodes in level " << processing_level);
            std::vector<TreeNode> tree_nodes_in_next_level;
            uint32_t processed_tree_nodes_in_level = 0;
            while(processed_tree_nodes_in_level < tree_nodes_in_level.size()) {
                TreeNode parent_node;
                //pack RTREE_BRANCHING_FACTOR elements into tree_nodes each
                for(uint32_t current_child_node_index = 0; RTREE_BRANCHING_FACTOR > current_child_node_index; ++current_child_node_index) {
                    if(processed_tree_nodes_in_level < tree_nodes_in_level.size()) {
                        TreeNode & current_child_node = tree_nodes_in_level[processed_tree_nodes_in_level];
                        //add tree node to parent entry
                        parent_node.children[current_child_node_index] = m_search_tree.size();
                        m_search_tree.push_back(current_child_node);
                        //augment MBR of parent
                        parent_node.minimum_bounding_rectangle.AugmentMBRectangle(current_child_node.minimum_bounding_rectangle);
                        //increase counters
                        ++parent_node.child_count;
                        ++processed_tree_nodes_in_level;
                    }
                }
                tree_nodes_in_next_level.push_back(parent_node);
                //				INFO("processed: " << processed_tree_nodes_in_level << ", generating " << (uint32_t)tree_nodes_in_next_level.size() << " parents");
            }
            tree_nodes_in_level.swap(tree_nodes_in_next_level);
            ++processing_level;
        }
        BOOST_ASSERT_MSG(1 == tree_nodes_in_level.size(), "tree broken, more than one root node");
        //last remaining entry is the root node;
        //		INFO("root node has " << (uint32_t)tree_nodes_in_level[0].child_count << " children");
        //store root node
        m_search_tree.push_back(tree_nodes_in_level[0]);

        //reverse and renumber tree to have root at index 0
        std::reverse(m_search_tree.begin(), m_search_tree.end());
#pragma omp parallel for schedule(guided)
        for(uint32_t i = 0; i < m_search_tree.size(); ++i) {
            TreeNode & current_tree_node = m_search_tree[i];
            for(uint32_t j = 0; j < current_tree_node.child_count; ++j) {
                const uint32_t old_id = current_tree_node.children[j];
                const uint32_t new_id = m_search_tree.size() - old_id - 1;
                current_tree_node.children[j] = new_id;
            }
        }

        //open tree file
        std::ofstream tree_node_file(tree_node_filename, std::ios::binary);
        uint32_t size_of_tree = m_search_tree.size();
        BOOST_ASSERT_MSG(0 < size_of_tree, "tree empty");
        tree_node_file.write((char *)&size_of_tree, sizeof(uint32_t));
        tree_node_file.write((char *)&m_search_tree[0], sizeof(TreeNode)*size_of_tree);
        //close tree node file.
        tree_node_file.close();
        double time2 = get_timestamp();
//        INFO("written " << processed_objects_count << " leafs in " << sizeof(LeafNode)*(1+(unsigned)std::ceil(processed_objects_count/RTREE_LEAF_NODE_SIZE) )+sizeof(uint64_t) << " bytes");
//        INFO("written search tree of " << size_of_tree << " tree nodes in " << sizeof(TreeNode)*size_of_tree+sizeof(uint32_t) << " bytes");
        INFO("finished r-tree construction in " << (time2-time1) << " seconds");

        //todo: test queries
/*        INFO("first MBR:" << m_search_tree[0].minimum_bounding_rectangle);

        DataT result;
        time1 = get_timestamp();
        bool found_nearest = NearestNeighbor(_Coordinate(50.191085,8.466479), result);
        time2 = get_timestamp();
        INFO("found nearest element to (50.191085,8.466479): " << (found_nearest ? "yes" : "no")  << " in " << (time2-time1) << "s at (" << result.lat1/100000. << "," << result.lon1/100000. << " " << result.lat2/100000. << "," << result.lon2/100000. << ")");
        time1 = get_timestamp();
        found_nearest = NearestNeighbor(_Coordinate(50.23979, 8.51882), result);
        time2 = get_timestamp();
        INFO("found nearest element to (50.23979, 8.51882): " << (found_nearest ? "yes" : "no")  << " in " << (time2-time1) << "s at (" << result.lat1/100000. << "," << result.lon1/100000. << " " << result.lat2/100000. << "," << result.lon2/100000. << ")");
        time1 = get_timestamp();
        found_nearest = NearestNeighbor(_Coordinate(49.0316,2.6937), result);
        time2 = get_timestamp();
        INFO("found nearest element to (49.0316,2.6937): " << (found_nearest ? "yes" : "no")  << " in " << (time2-time1) << "s at (" << result.lat1/100000. << "," << result.lon1/100000. << " " << result.lat2/100000. << "," << result.lon2/100000. << ")");
*/
    }

    //Read-only operation for queries
    explicit StaticRTree(
            const char * node_filename,
            const char * leaf_filename
    ) : m_leaf_node_filename(leaf_filename) {
        //INFO("Loading nodes: " << node_filename);
        //INFO("opening leafs: " << leaf_filename);
        //open tree node file and load into RAM.
        std::ifstream tree_node_file(node_filename, std::ios::binary);
        uint32_t tree_size = 0;
        tree_node_file.read((char*)&tree_size, sizeof(uint32_t));
        //INFO("reading " << tree_size << " tree nodes in " << (sizeof(TreeNode)*tree_size) << " bytes");
        m_search_tree.resize(tree_size);
        tree_node_file.read((char*)&m_search_tree[0], sizeof(TreeNode)*tree_size);
        tree_node_file.close();

        //open leaf node file and store thread specific pointer
        std::ifstream leaf_node_file(leaf_filename, std::ios::binary);
        leaf_node_file.read((char*)&m_element_count, sizeof(uint64_t));
        leaf_node_file.close();

        //INFO( tree_size << " nodes in search tree");
        //INFO( m_element_count << " elements in leafs");
    }
/*
    inline void FindKNearestPhantomNodesForCoordinate(
        const _Coordinate & location,
        const unsigned zoom_level,
        const unsigned candidate_count,
        std::vector<std::pair<PhantomNode, double> > & result_vector
    ) const {

        bool ignore_tiny_components = (zoom_level <= 14);
        DataT nearest_edge;

        uint32_t io_count = 0;
        uint32_t explored_tree_nodes_count = 0;
        INFO("searching for coordinate " << input_coordinate);
        double min_dist = DBL_MAX;
        double min_max_dist = DBL_MAX;
        bool found_a_nearest_edge = false;

        _Coordinate nearest, current_start_coordinate, current_end_coordinate;

        //initialize queue with root element
        std::priority_queue<QueryCandidate> traversal_queue;
        traversal_queue.push(QueryCandidate(0, m_search_tree[0].minimum_bounding_rectangle.GetMinDist(input_coordinate)));
        BOOST_ASSERT_MSG(FLT_EPSILON > (0. - traversal_queue.top().min_dist), "Root element in NN Search has min dist != 0.");

        while(!traversal_queue.empty()) {
            const QueryCandidate current_query_node = traversal_queue.top(); traversal_queue.pop();

            ++explored_tree_nodes_count;
            bool prune_downward = (current_query_node.min_dist >= min_max_dist);
            bool prune_upward    = (current_query_node.min_dist >= min_dist);
            if( !prune_downward && !prune_upward ) { //downward pruning
                TreeNode & current_tree_node = m_search_tree[current_query_node.node_id];
                if (current_tree_node.child_is_on_disk) {
                    LeafNode current_leaf_node;
                    LoadLeafFromDisk(current_tree_node.children[0], current_leaf_node);
                    ++io_count;
                    for(uint32_t i = 0; i < current_leaf_node.object_count; ++i) {
                        DataT & current_edge = current_leaf_node.objects[i];
                        if(ignore_tiny_components && current_edge.belongsToTinyComponent) {
                            continue;
                        }

                        double current_ratio = 0.;
                        double current_perpendicular_distance = ComputePerpendicularDistance(
                                                                                             input_coordinate,
                                                                                             _Coordinate(current_edge.lat1, current_edge.lon1),
                                                                                             _Coordinate(current_edge.lat2, current_edge.lon2),
                                                                                             nearest,
                                                                                             &current_ratio
                                                                                             );

                        if(
                           current_perpendicular_distance < min_dist
                           && !DoubleEpsilonCompare(
                                                    current_perpendicular_distance,
                                                    min_dist
                                                    )
                           ) { //found a new minimum
                            min_dist = current_perpendicular_distance;
                            result_phantom_node.edgeBasedNode = current_edge.id;
                            result_phantom_node.nodeBasedEdgeNameID = current_edge.nameID;
                            result_phantom_node.weight1 = current_edge.weight;
                            result_phantom_node.weight2 = INT_MAX;
                            result_phantom_node.location = nearest;
                            current_start_coordinate.lat = current_edge.lat1;
                            current_start_coordinate.lon = current_edge.lon1;
                            current_end_coordinate.lat = current_edge.lat2;
                            current_end_coordinate.lon = current_edge.lon2;
                            nearest_edge = current_edge;
                            found_a_nearest_edge = true;
                        } else if(
                                  DoubleEpsilonCompare(current_perpendicular_distance, min_dist) &&
                                  1 == abs(current_edge.id - result_phantom_node.edgeBasedNode )
                                  && CoordinatesAreEquivalent(
                                                              current_start_coordinate,
                                                              _Coordinate(
                                                                          current_edge.lat1,
                                                                          current_edge.lon1
                                                                          ),
                                                              _Coordinate(
                                                                          current_edge.lat2,
                                                                          current_edge.lon2
                                                                          ),
                                                              current_end_coordinate
                                                              )
                                  ) {
                            result_phantom_node.edgeBasedNode = std::min(current_edge.id, result_phantom_node.edgeBasedNode);
                            result_phantom_node.weight2 = current_edge.weight;
                        }
                    }
                } else {
                    //traverse children, prune if global mindist is smaller than local one
                    for (uint32_t i = 0; i < current_tree_node.child_count; ++i) {
                        const int32_t child_id = current_tree_node.children[i];
                        TreeNode & child_tree_node = m_search_tree[child_id];
                        RectangleT & child_rectangle = child_tree_node.minimum_bounding_rectangle;
                        const double current_min_dist = child_rectangle.GetMinDist(input_coordinate);
                        const double current_min_max_dist = child_rectangle.GetMinMaxDist(input_coordinate);
                        if( current_min_max_dist < min_max_dist ) {
                            min_max_dist = current_min_max_dist;
                        }
                        if (current_min_dist > min_max_dist) {
                            continue;
                        }
                        if (current_min_dist > min_dist) { //upward pruning
                            continue;
                        }
                        traversal_queue.push(QueryCandidate(child_id, current_min_dist));
                    }
                }
            }
        }

        const double ratio = (found_a_nearest_edge ?
                              std::min(1., ApproximateDistance(_Coordinate(nearest_edge.lat1, nearest_edge.lon1),
                                                               result_phantom_node.location)/ApproximateDistance(_Coordinate(nearest_edge.lat1, nearest_edge.lon1), _Coordinate(nearest_edge.lat2, nearest_edge.lon2))
                                       ) : 0
                              );
        result_phantom_node.weight1 *= ratio;
        if(INT_MAX != result_phantom_node.weight2) {
            result_phantom_node.weight2 *= (1.-ratio);
        }
        result_phantom_node.ratio = ratio;

        //Hack to fix rounding errors and wandering via nodes.
        if(std::abs(input_coordinate.lon - result_phantom_node.location.lon) == 1) {
            result_phantom_node.location.lon = input_coordinate.lon;
        }
        if(std::abs(input_coordinate.lat - result_phantom_node.location.lat) == 1) {
            result_phantom_node.location.lat = input_coordinate.lat;
        }

        INFO("mindist: " << min_dist << ", io's: " << io_count << ", nodes: " << explored_tree_nodes_count << ", loc: " << result_phantom_node.location << ", ratio: " << ratio << ", id: " << result_phantom_node.edgeBasedNode);
        INFO("bidirected: " << (result_phantom_node.isBidirected() ? "yes" : "no") );
        return found_a_nearest_edge;

    }

  */
    bool FindPhantomNodeForCoordinate(
            const _Coordinate & input_coordinate,
            PhantomNode & result_phantom_node,
            const unsigned zoom_level
    ) {

        bool ignore_tiny_components = (zoom_level <= 14);
        DataT nearest_edge;

        uint32_t io_count = 0;
        uint32_t explored_tree_nodes_count = 0;
        //INFO("searching for coordinate " << input_coordinate);
        double min_dist = DBL_MAX;
        double min_max_dist = DBL_MAX;
        bool found_a_nearest_edge = false;

        _Coordinate nearest, current_start_coordinate, current_end_coordinate;

        //initialize queue with root element
        std::priority_queue<QueryCandidate> traversal_queue;
        double current_min_dist = m_search_tree[0].minimum_bounding_rectangle.GetMinDist(input_coordinate);
        traversal_queue.push(
                             QueryCandidate(0, current_min_dist)
        );

        BOOST_ASSERT_MSG(
                         FLT_EPSILON > (0. - traversal_queue.top().min_dist),
                         "Root element in NN Search has min dist != 0."
        );

        while(!traversal_queue.empty()) {
            const QueryCandidate current_query_node = traversal_queue.top(); traversal_queue.pop();

            ++explored_tree_nodes_count;
            bool prune_downward = (current_query_node.min_dist >= min_max_dist);
            bool prune_upward    = (current_query_node.min_dist >= min_dist);
            if( !prune_downward && !prune_upward ) { //downward pruning
                TreeNode & current_tree_node = m_search_tree[current_query_node.node_id];
                if (current_tree_node.child_is_on_disk) {
                    LeafNode current_leaf_node;
                    LoadLeafFromDisk(current_tree_node.children[0], current_leaf_node);
                    ++io_count;
                    //INFO("checking " << current_leaf_node.object_count << " elements");
                    for(uint32_t i = 0; i < current_leaf_node.object_count; ++i) {
                        DataT & current_edge = current_leaf_node.objects[i];
                        if(ignore_tiny_components && current_edge.belongsToTinyComponent) {
                            continue;
                        }
                        if(current_edge.isIgnored()) {
                            continue;
                        }

                       double current_ratio = 0.;
                       double current_perpendicular_distance = ComputePerpendicularDistance(
                                input_coordinate,
                                _Coordinate(current_edge.lat1, current_edge.lon1),
                                _Coordinate(current_edge.lat2, current_edge.lon2),
                                nearest,
                                &current_ratio
                        );

                        //INFO("[" << current_edge.id << "] (" << current_edge.lat1/100000. << "," << current_edge.lon1/100000. << ")==(" << current_edge.lat2/100000. << "," << current_edge.lon2/100000. << ") at distance " << current_perpendicular_distance << " min dist: " << min_dist
                        //     << ", ratio " << current_ratio
                        //     );

                        if(
                                current_perpendicular_distance < min_dist
                                && !DoubleEpsilonCompare(
                                        current_perpendicular_distance,
                                        min_dist
                                )
                        ) { //found a new minimum
                            min_dist = current_perpendicular_distance;
                            result_phantom_node.edgeBasedNode = current_edge.id;
                            result_phantom_node.nodeBasedEdgeNameID = current_edge.nameID;
                            result_phantom_node.weight1 = current_edge.weight;
                            result_phantom_node.weight2 = INT_MAX;
                            result_phantom_node.location = nearest;
                            current_start_coordinate.lat = current_edge.lat1;
                            current_start_coordinate.lon = current_edge.lon1;
                            current_end_coordinate.lat = current_edge.lat2;
                            current_end_coordinate.lon = current_edge.lon2;
                            nearest_edge = current_edge;
                            found_a_nearest_edge = true;
                        } else if(
                                DoubleEpsilonCompare(current_perpendicular_distance, min_dist) &&
                                1 == abs(current_edge.id - result_phantom_node.edgeBasedNode )
                        && CoordinatesAreEquivalent(
                                current_start_coordinate,
                                _Coordinate(
                                        current_edge.lat1,
                                        current_edge.lon1
                                ),
                                _Coordinate(
                                        current_edge.lat2,
                                        current_edge.lon2
                                ),
                                current_end_coordinate
                            )
                        ) {
                            BOOST_ASSERT_MSG(current_edge.id != result_phantom_node.edgeBasedNode, "IDs not different");
                            //INFO("found bidirected edge on nodes " << current_edge.id << " and " << result_phantom_node.edgeBasedNode);
                            result_phantom_node.weight2 = current_edge.weight;
                            if(current_edge.id < result_phantom_node.edgeBasedNode) {
                                result_phantom_node.edgeBasedNode = current_edge.id;
                                std::swap(result_phantom_node.weight1, result_phantom_node.weight2);
                                std::swap(current_end_coordinate, current_start_coordinate);
                            //    INFO("case 2");
                            }
                            //INFO("w1: " << result_phantom_node.weight1 << ", w2: " << result_phantom_node.weight2);
                        }
                    }
                } else {
                    //traverse children, prune if global mindist is smaller than local one
                    for (uint32_t i = 0; i < current_tree_node.child_count; ++i) {
                        const int32_t child_id = current_tree_node.children[i];
                        TreeNode & child_tree_node = m_search_tree[child_id];
                        RectangleT & child_rectangle = child_tree_node.minimum_bounding_rectangle;
                        const double current_min_dist = child_rectangle.GetMinDist(input_coordinate);
                        const double current_min_max_dist = child_rectangle.GetMinMaxDist(input_coordinate);
                        if( current_min_max_dist < min_max_dist ) {
                            min_max_dist = current_min_max_dist;
                        }
                        if (current_min_dist > min_max_dist) {
                            continue;
                        }
                        if (current_min_dist > min_dist) { //upward pruning
                            continue;
                        }
                        traversal_queue.push(QueryCandidate(child_id, current_min_dist));
                    }
                }
            }
        }

        const double ratio = (found_a_nearest_edge ?
            std::min(1., ApproximateDistance(current_start_coordinate,
                result_phantom_node.location)/ApproximateDistance(current_start_coordinate, current_end_coordinate)
                ) : 0
            );
        result_phantom_node.weight1 *= ratio;
        if(INT_MAX != result_phantom_node.weight2) {
            result_phantom_node.weight2 *= (1.-ratio);
        }
        result_phantom_node.ratio = ratio;

        //Hack to fix rounding errors and wandering via nodes.
        if(std::abs(input_coordinate.lon - result_phantom_node.location.lon) == 1) {
            result_phantom_node.location.lon = input_coordinate.lon;
        }
        if(std::abs(input_coordinate.lat - result_phantom_node.location.lat) == 1) {
            result_phantom_node.location.lat = input_coordinate.lat;
        }

//        INFO("start: (" << nearest_edge.lat1 << "," << nearest_edge.lon1 << "), end: (" << nearest_edge.lat2 << "," << nearest_edge.lon2 << ")" );
//        INFO("mindist: " << min_dist << ", io's: " << io_count << ", nodes: " << explored_tree_nodes_count << ", loc: " << result_phantom_node.location << ", ratio: " << ratio << ", id: " << result_phantom_node.edgeBasedNode);
//        INFO("weight1: " << result_phantom_node.weight1 << ", weight2: " << result_phantom_node.weight2);
//        INFO("bidirected: " << (result_phantom_node.isBidirected() ? "yes" : "no") );
//        INFO("NameID: " << result_phantom_node.nodeBasedEdgeNameID);
        return found_a_nearest_edge;

    }
/*
    //Nearest-Neighbor query with the Roussopoulos et al. algorithm [2]
    inline bool NearestNeighbor(const _Coordinate & input_coordinate, DataT & result_element) {
        uint32_t io_count = 0;
        uint32_t explored_tree_nodes_count = 0;
        INFO("searching for coordinate " << input_coordinate);
        double min_dist = DBL_MAX;
        double min_max_dist = DBL_MAX;
        bool found_return_value = false;

        //initialize queue with root element
        std::priority_queue<QueryCandidate> traversal_queue;
        traversal_queue.push(QueryCandidate(0, m_search_tree[0].minimum_bounding_rectangle.GetMinDist(input_coordinate)));
        BOOST_ASSERT_MSG(FLT_EPSILON > (0. - traversal_queue.top().min_dist), "Root element in NN Search has min dist != 0.");

        while(!traversal_queue.empty()) {
            const QueryCandidate current_query_node = traversal_queue.top(); traversal_queue.pop();

            ++explored_tree_nodes_count;

            //			INFO("popped node " << current_query_node.node_id << " at distance " << current_query_node.min_dist);
            bool prune_downward = (current_query_node.min_dist >= min_max_dist);
            bool prune_upward    = (current_query_node.min_dist >= min_dist);
            //			INFO(" up prune:   " << (prune_upward ? "y" : "n" ));
            //			INFO(" down prune: " << (prune_downward ? "y" : "n" ));
            if( prune_downward || prune_upward ) { //downward pruning
                //				INFO("  pruned node " << current_query_node.node_id << " because " << current_query_node.min_dist << "<" << min_max_dist);
            } else {
                TreeNode & current_tree_node = m_search_tree[current_query_node.node_id];
                if (current_tree_node.child_is_on_disk) {
                    //					INFO("  Fetching child from disk for id: " << current_query_node.node_id);
                    LeafNode current_leaf_node;
                    LoadLeafFromDisk(current_tree_node.children[0], current_leaf_node);
                    ++io_count;
                    double ratio = 0.;
                    _Coordinate nearest;
                    for(uint32_t i = 0; i < current_leaf_node.object_count; ++i) {
                        DataT & current_object = current_leaf_node.objects[i];
                        double current_perpendicular_distance = ComputePerpendicularDistance(
                                input_coordinate,
                                _Coordinate(current_object.lat1, current_object.lon1),
                                _Coordinate(current_object.lat2, current_object.lon2),
                                nearest,
                                &ratio
                        );

                        if(current_perpendicular_distance < min_dist && !DoubleEpsilonCompare(current_perpendicular_distance, min_dist)) { //found a new minimum
                            min_dist = current_perpendicular_distance;
                            result_element = current_object;
                            found_return_value = true;
                        }
                    }
                } else {
                    //traverse children, prune if global mindist is smaller than local one
                    //					INFO("  Checking " << current_tree_node.child_count << " children of node " << current_query_node.node_id);
                    for (uint32_t i = 0; i < current_tree_node.child_count; ++i) {
                        const int32_t child_id = current_tree_node.children[i];
                        TreeNode & child_tree_node = m_search_tree[child_id];
                        RectangleT & child_rectangle = child_tree_node.minimum_bounding_rectangle;
                        const double current_min_dist = child_rectangle.GetMinDist(input_coordinate);
                        const double current_min_max_dist = child_rectangle.GetMinMaxDist(input_coordinate);
                        if( current_min_max_dist < min_max_dist ) {
                            min_max_dist = current_min_max_dist;
                        }
                        if (current_min_dist > min_max_dist) {
                            continue;
                        }
                        if (current_min_dist > min_dist) { //upward pruning
                            continue;
                        }
                        //						INFO("   pushing node " << child_id << " at distance " << current_min_dist);
                        traversal_queue.push(QueryCandidate(child_id, current_min_dist));
                    }
                }
            }
        }
        INFO("mindist: " << min_dist << ", io's: " << io_count << ",  touched nodes: " << explored_tree_nodes_count);
        return found_return_value;
    }
 */
private:
    inline void LoadLeafFromDisk(const uint32_t leaf_id, LeafNode& result_node) {
        if(!thread_local_rtree_stream.get() || !thread_local_rtree_stream->is_open()) {
            thread_local_rtree_stream.reset(
                new std::ifstream(
                        m_leaf_node_filename.c_str(),
                        std::ios::in | std::ios::binary
                )
            );
        }
        if(!thread_local_rtree_stream->good()) {
            thread_local_rtree_stream->clear(std::ios::goodbit);
            DEBUG("Resetting stale filestream");
        }
        uint64_t seek_pos = sizeof(uint64_t) + leaf_id*sizeof(LeafNode);
        thread_local_rtree_stream->seekg(seek_pos);
        thread_local_rtree_stream->read((char *)&result_node, sizeof(LeafNode));
    }

    inline double ComputePerpendicularDistance(
            const _Coordinate& inputPoint,
            const _Coordinate& source,
            const _Coordinate& target,
            _Coordinate& nearest, double *r) const {
        const double x = static_cast<double>(inputPoint.lat);
        const double y = static_cast<double>(inputPoint.lon);
        const double a = static_cast<double>(source.lat);
        const double b = static_cast<double>(source.lon);
        const double c = static_cast<double>(target.lat);
        const double d = static_cast<double>(target.lon);
        double p,q,mX,nY;
        if(fabs(a-c) > FLT_EPSILON){
            const double m = (d-b)/(c-a); // slope
            // Projection of (x,y) on line joining (a,b) and (c,d)
            p = ((x + (m*y)) + (m*m*a - m*b))/(1. + m*m);
            q = b + m*(p - a);
        } else {
            p = c;
            q = y;
        }
        nY = (d*p - c*q)/(a*d - b*c);
        mX = (p - nY*a)/c;// These values are actually n/m+n and m/m+n , we need
        // not calculate the explicit values of m an n as we
        // are just interested in the ratio
        if(std::isnan(mX)) {
            *r = (target == inputPoint) ? 1. : 0.;
        } else {
            *r = mX;
        }
        if(*r<=0.){
            nearest.lat = source.lat;
            nearest.lon = source.lon;
            return ((b - y)*(b - y) + (a - x)*(a - x));
//            return std::sqrt(((b - y)*(b - y) + (a - x)*(a - x)));
        } else if(*r >= 1.){
            nearest.lat = target.lat;
            nearest.lon = target.lon;
            return ((d - y)*(d - y) + (c - x)*(c - x));
//            return std::sqrt(((d - y)*(d - y) + (c - x)*(c - x)));
        }
        // point lies in between
        nearest.lat = p;
        nearest.lon = q;
//        return std::sqrt((p-x)*(p-x) + (q-y)*(q-y));
        return (p-x)*(p-x) + (q-y)*(q-y);
    }

    inline bool CoordinatesAreEquivalent(const _Coordinate & a, const _Coordinate & b, const _Coordinate & c, const _Coordinate & d) const {
        return (a == b && c == d) || (a == c && b == d) || (a == d && b == c);
    }

    inline bool DoubleEpsilonCompare(const double d1, const double d2) const {
        return (std::fabs(d1 - d2) < FLT_EPSILON);
    }

};

//[1] "On Packing R-Trees"; I. Kamel, C. Faloutsos; 1993; DOI: 10.1145/170088.170403
//[2] "Nearest Neighbor Queries", N. Roussopulos et al; 1995; DOI: 10.1145/223784.223794


#endif /* STATICRTREE_H_ */
