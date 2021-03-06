/**
*
* Copyright (C) 2014-2018    Andrei Novikov (pyclustering@yandex.ru)
*
* GNU_PUBLIC_LICENSE
*   pyclustering is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   pyclustering is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include "cluster/kmeans.hpp"

#include <algorithm>
#include <limits>

#include "utils/metric.hpp"


using namespace ccore::utils::metric;


namespace ccore {

namespace clst {


const double             kmeans::DEFAULT_TOLERANCE                       = 0.025;

const std::size_t        kmeans::DEFAULT_DATA_SIZE_PARALLEL_PROCESSING   = 200000;

const std::size_t        kmeans::DEFAULT_MAX_THREAD_POOL_SIZE            = 15;


kmeans::kmeans(const dataset & p_initial_centers, const double p_tolerance) :
    m_tolerance(p_tolerance * p_tolerance),
    m_initial_centers(p_initial_centers),
    m_ptr_result(nullptr),
    m_ptr_data(nullptr),
    m_parallel_trigger(DEFAULT_DATA_SIZE_PARALLEL_PROCESSING),
    m_parallel_processing(false),
    m_mutex(),
    m_pool(nullptr)
{ }


kmeans::~kmeans(void) { }


void kmeans::process(const dataset & data, cluster_data & output_result) {
    m_ptr_data = &data;

    output_result = kmeans_data();
    m_ptr_result = (kmeans_data *) &output_result;

    if (data[0].size() != m_initial_centers[0].size()) {
        throw std::runtime_error("CCORE [kmeans]: dimension of the input data and dimension of the initial cluster centers must be equal.");
    }

    m_parallel_processing = (m_ptr_data->size() >= m_parallel_trigger);
    if (m_parallel_processing) {
        std::size_t pool_size = m_initial_centers.size();
        if (pool_size > DEFAULT_MAX_THREAD_POOL_SIZE) {
            pool_size = DEFAULT_MAX_THREAD_POOL_SIZE;
        }

        m_pool = std::make_shared<thread_pool>(pool_size);
    }

    m_ptr_result->centers()->assign(m_initial_centers.begin(), m_initial_centers.end());

    double current_change = std::numeric_limits<double>::max();

    while(current_change > m_tolerance) {
        update_clusters(*m_ptr_result->centers(), *m_ptr_result->clusters());
        current_change = update_centers(*m_ptr_result->clusters(), *m_ptr_result->centers());
    }
}


void kmeans::set_parallel_processing_trigger(const std::size_t p_data_size) {
    m_parallel_trigger = p_data_size;
}


void kmeans::update_clusters(const dataset & centers, cluster_sequence & clusters) {
    const dataset & data = *m_ptr_data;

    clusters.clear();
    clusters.resize(centers.size());

    /* fill clusters again in line with centers. */
    for (size_t index_object = 0; index_object < data.size(); index_object++) {
        double    minimum_distance = std::numeric_limits<double>::max();
        size_t    suitable_index_cluster = 0;

        for (size_t index_cluster = 0; index_cluster < clusters.size(); index_cluster++) {
            double distance = euclidean_distance_sqrt(&centers[index_cluster], &data[index_object]);

            if (distance < minimum_distance) {
                minimum_distance = distance;
                suitable_index_cluster = index_cluster;
            }
        }

        clusters[suitable_index_cluster].push_back(index_object);
    }

    erase_empty_clusters(clusters);
}


void kmeans::erase_empty_clusters(cluster_sequence & p_clusters) {
    for (size_t index_cluster = p_clusters.size() - 1; index_cluster != (size_t) -1; index_cluster--) {
        if (p_clusters[index_cluster].empty()) {
            p_clusters.erase(p_clusters.begin() + index_cluster);
        }
    }
}


double kmeans::update_centers(const cluster_sequence & clusters, dataset & centers) {
    const dataset & data = *m_ptr_data;
    const size_t dimension = data[0].size();

    dataset calculated_clusters(clusters.size(), point(dimension, 0.0));
    std::vector<double> changes(clusters.size(), 0.0);

    if (m_parallel_processing) {
        for (size_t index_cluster = 0; index_cluster < clusters.size(); index_cluster++) {
            calculated_clusters[index_cluster] = centers[index_cluster];

            task::proc update_proc = [this, index_cluster, &clusters, &calculated_clusters, &changes]() {
                changes[index_cluster] = update_center(clusters[index_cluster], calculated_clusters[index_cluster]);
            };

            m_pool->add_task(update_proc);
        }

        for (std::size_t index_cluster = 0; index_cluster < clusters.size(); index_cluster++) {
            m_pool->pop_complete_task();
        }
    }
    else {
        for (size_t index_cluster = 0; index_cluster < clusters.size(); index_cluster++) {
            calculated_clusters[index_cluster] = centers[index_cluster];
            changes[index_cluster] = update_center(clusters[index_cluster], calculated_clusters[index_cluster]);
        }
    }

    centers = std::move(calculated_clusters);

    return *(std::max_element(changes.begin(), changes.end()));
}


double kmeans::update_center(const cluster & p_cluster, point & p_center) {
    point total(p_center.size(), 0.0);

    /* for each object in cluster */
    for (auto object_index : p_cluster) {
        /* for each dimension */
        for (size_t dimension = 0; dimension < total.size(); dimension++) {
            total[dimension] += (*m_ptr_data)[object_index][dimension];
        }
    }

    /* average for each dimension */
    for (size_t dimension = 0; dimension < total.size(); dimension++) {
        total[dimension] = total[dimension] / p_cluster.size();
    }

    double change = euclidean_distance_sqrt(&p_center, &total);

    p_center = std::move(total);
    return change;
}


}

}