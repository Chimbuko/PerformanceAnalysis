#pragma once
#include<array>
#include "chimbuko/ad/ADEvent.hpp"
#include "chimbuko/ad/ExecData.hpp"
#include "chimbuko/util/RunStats.hpp"
#include "chimbuko/param.hpp"
#ifdef _USE_MPINET
#include "chimbuko/net/mpi_net.hpp"
#else
#include "chimbuko/net/zmq_net.hpp"
#endif
#ifdef _PERF_METRIC
#include "chimbuko/util/RunMetric.hpp"
#endif

namespace chimbuko {

  template<typename T, size_t N>
  struct ArrayHasher {
    std::size_t operator()(const std::array<T, N>& a) const{
      std::size_t h = 0;
      
      for (auto e : a) {
	h ^= std::hash<T>{}(e)  + 0x9e3779b9 + (h << 6) + (h >> 2); 
      }
      return h;
    }   
  };

  
  /**
   * @brief abstract class for anomaly detection algorithms
   * 
   */
  class ADOutlier {

  public:
    /**
     * @brief Construct a new ADOutlier object
     * 
     */
    ADOutlier();
    /**
     * @brief Destroy the ADOutlier object
     * 
     */
    virtual ~ADOutlier();

    /**
     * @brief copy a pointer to execution data map 
     * 
     * @param m 
     * @see ADEvent
     */
    void linkExecDataMap(const ExecDataMap_t* m) { m_execDataMap = m; }

    /**
     * @brief check if the parameter server is in use
     * 
     * @return true if the parameter server is in use
     * @return false if the parameter server is not in use
     */
    bool use_ps() const { return m_use_ps; }
    /**
     * @brief connect to the parameter server
     * 
     * @param rank this process rank
     * @param srank server process rank
     * @param sname server name
     */
    void connect_ps(int rank, int srank = 0, std::string sname="MPINET");
    /**
     * @brief disconnect from the connected parameter server
     * 
     */
    void disconnect_ps();
    /**
     * @brief abstract method to run the implemented anomaly detection algorithm
     * 
     * @param step step (or frame) number
     * @return unsigned long the number of detected anomalies
     */
    virtual unsigned long run(int step=0) = 0;

#ifdef _PERF_METRIC
    void dump_perf(std::string path, std::string filename="metric.json"){
      m_perf.dump(path, filename);
    }
#endif

    /**
     * @brief Get the local copy of the global parameters
     * @return Pointer to a ParamInterface object
     */
    ParamInterface const* get_global_parameters() const{ return m_param; }
  
  protected:
    /**
     * @brief abstract method to compute outliers (or anomalies)
     * 
     * @param func_id function id
     * @param data a list of function calls to inspect
     * @param min_ts the minimum timestamp of the list of function calls
     * @param max_ts the maximum timestamp of the list of function calls
     * @return unsigned long the number of outliers (or anomalies)
     */
    virtual unsigned long compute_outliers(
					   const unsigned long func_id, std::vector<CallListIterator_t>& data,
					   long& min_ts, long& max_ts) = 0;

    /**
     * @brief abstract method to update local parameters and get global ones
     * 
     * @param[in] param local parameters
     * @return std::pair<size_t, size_t> [sent, recv] message size 
     */
    virtual std::pair<size_t, size_t> sync_param(ParamInterface const* param) = 0;
    /**
     * @brief update (send) local anomaly statistics to the connected parameter server
     * 
     * @param l_stats local statistics
     * @param step step (or frame) number
     * @return std::pair<size_t, size_t> [sent, recv] message size
     */
    std::pair<size_t, size_t> update_local_statistics(std::string l_stats, int step);

  protected:
    bool m_use_ps;                           /**< true if the parameter server is in use */
    int m_rank;                              /**< this process rank                      */
    int m_srank;                             /**< server process rank                    */
#ifdef _USE_MPINET
    MPI_Comm m_comm;
#else
    void* m_context;                         /**< ZeroMQ context */
    void* m_socket;                          /**< ZeroMQ socket */
#endif
 
    std::unordered_map< std::array<unsigned long, 4>, size_t, ArrayHasher<unsigned long,4> > m_local_func_exec_count; /**< Map(program id, rank id, thread id, func id) -> number of times encountered on this node*/
    
    const ExecDataMap_t * m_execDataMap;     /**< execution data map */
    ParamInterface * m_param;                /**< global parameters (kept in sync with parameter server) */

#ifdef _PERF_METRIC
    RunMetric m_perf;
#endif
    // number of outliers per function: func id -> # outliers
    // std::unordered_map<unsigned long, unsigned long> m_outliers;
    // inclusive runtime statistics per fucntion: func id -> run stats
    // exclusive runtime statistics per function: func id -> run stats
  };

  /**
   * @brief statistic analysis based anomaly detection algorithm
   * 
   */
  class ADOutlierSSTD : public ADOutlier {

  public:
    /**
     * @brief Construct a new ADOutlierSSTD object
     * 
     */
    ADOutlierSSTD();
    /**
     * @brief Destroy the ADOutlierSSTD object
     * 
     */
    ~ADOutlierSSTD();

    /**
     * @brief Set the sigma value
     * 
     * @param sigma sigma value
     */
    void set_sigma(double sigma) { m_sigma = sigma; }

    /**
     * @brief run this anomaly detection algorithm
     * 
     * @param step step (or frame) number
     * @return unsigned long the number of anomalies
     */
    unsigned long run(int step=0) override;

  protected:
    /**
     * @brief compute outliers (or anomalies) of the list of function calls
     * 
     * @param func_id function id
     * @param data a list of function calls to inspect
     * @param min_ts the minimum timestamp of the list of function calls
     * @param max_ts the maximum timestamp of the list of function calls
     * @return unsigned long the number of outliers (or anomalies)
     */
    unsigned long compute_outliers(
				   const unsigned long func_id, std::vector<CallListIterator_t>& data,
				   long& min_ts, long& max_ts) override;


    std::pair<size_t, size_t> sync_param(ParamInterface const* param) override;
    
  private:
    double m_sigma; /**< sigma */
  };

} // end of AD namespace
