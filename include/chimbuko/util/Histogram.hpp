#pragma once

#include<vector>
#include <nlohmann/json.hpp>

namespace chimbuko{

  /**
   * @brief Base class for functional that determines the histogram bin width
   */
  struct binWidthSpecifier{
    virtual double bin_width(const std::vector<double> &runtimes, const double min, const double max) const = 0;
    virtual ~binWidthSpecifier(){}
  };
  /**
   * @brief Use a fixed bin width
   */
  struct binWidthFixed: public binWidthSpecifier{
    double width;
    binWidthFixed(double width): width(width){}

    double bin_width(const std::vector<double> &runtimes, const double min, const double max) const override{ return width; }
  };
  /**
   * @brief Determine the bin width using Scott's rule
   */
  struct binWidthScott: public binWidthSpecifier{
    double bin_width(const std::vector<double> &runtimes, const double min, const double max) const override;
  };
  /**
   * @brief Determine the bin width from the range assuming a fixed number of bins
   */
  struct binWidthFixedNbin: public binWidthSpecifier{
    int nbin;
    binWidthFixedNbin(int nbin): nbin(nbin){}
    double bin_width(const std::vector<double> &runtimes, const double min, const double max) const override;
  };

  /**
   * @brief Histogram Implementation
   *
   * Note: lower bin edges are exclusive and upper edges inclusive,  i.e. for bin b with edges  (l,u), all data v in the bin have  l < v <= u
   */
  class Histogram {

  public:

    /**
     * @brief Construct a Histogram object
     */
    Histogram();

    /**
     * @brief Destroy Histogram object
     */
    ~Histogram();


    /**
     * @brief Construct a Histogram object from the data provided
     * @param data a vector<double> of data
     * @param bwspec a functional that specifies the bin width. Default to using Scott's rule
     */
    Histogram(const std::vector<double> &data, const binWidthSpecifier &bwspec = binWidthScott());


    /**
     * @brief Data structure that stores Histogram data ( bin counts, bin edges)
     */
    struct Data {

      double glob_threshold; /**< global threshold used to filter anomalies*/
      std::vector<int> counts; /**< Bin counts in Histogram*/
      std::vector<double> bin_edges; /**< Bin edges in Histogram*/

      /**
       * @brief Initialize histogram data
       */
      Data(){
        clear();
      }

      /**
       * @brief Initialize histogram data with existing histogram data
       * @param g_threshold: Global Threshold
       * @param h_counts: a vector<int> of histogram bin counts
       * @param h_bin_edges: a vector<double> of histogram bin edges
       */
      Data(const double& g_threshold, const std::vector<int>& h_counts, const std::vector<double>& h_bin_edges ) {
        glob_threshold = g_threshold;
        counts = h_counts;
        bin_edges = h_bin_edges;
      }

      /**
       * @brief Resets histogram data during initialization
       */
      void clear() {
        glob_threshold = log2(1.00001);
        counts.clear();
        bin_edges.clear();
      }

      /**
       * @brief Serialize using cereal
       */
      template<class Archive>
      void serialize(Archive & archive){
         archive(glob_threshold, counts, bin_edges);
      }

      /**
       * @brief Comparison operator
       */
      bool operator==(const Data &r) const{ return glob_threshold == r.glob_threshold && counts == r.counts && bin_edges == r.bin_edges; }


    };


    void clear() {m_histogram.clear();}

    void push (double x);

    /**
     * @brief returns reference to current histogram Data
     * @return Data: Histogram data (bin counts, bin edges)
     */
    const Data &get_histogram() const{ return m_histogram; }

    /**
     * @brief Set the internal variables from an instance of Histogram Data
     * @param d: Histogram Data (bin counts, bin edges)
     */
    void set_hist_data(const Data& d);

    /**
     * @brief Create an instance of this class from a Histogram Data instance
     * @param d: Histogram Data (bin counts, bin edges)
     * @return Instance of Histogram
     */
    static Histogram from_hist_data(const Data& d) {
      Histogram histdata;
      histdata.set_hist_data(d);
      return histdata;
    }

    /**
     * @brief Create new histogram locally for AD module's batch data instances
     * @param r_times a vector<double> of function run times
     * @param bwspec a functional that specifies the bin width. Default to using Scott's rule
     */
    void create_histogram(const std::vector<double>& r_times, const binWidthSpecifier &bwspec = binWidthScott());

    /**
     * @brief merges a Histogram with function runtimes
     * @param g: Histogram to merge
     * @param runtimes: Function runtimes
     * @param bwspec a functional that specifies the bin width of the merged histogram. Default to using Scott's rule
     */
    void merge_histograms(const Histogram& g, const std::vector<double>& runtimes, const binWidthSpecifier &bwspec = binWidthScott());

    /**
     * @brief Combine two Histogram instances such that the resulting statistics are the union of the two Histograms
     * @param g: Histogram to merge into
     * @param l: Histogram to merge
     * @return result: Merged Histogram
     */
    friend Histogram operator+(const Histogram& g, const Histogram& l);

    /**
     * @brief Combine two Histogram instances such that the resulting statistics are the union of the two Histograms
     * @param h: Histogram to merge
     * @return result: Merged Histogram
     */
    Histogram & operator+=(const Histogram& h);


    /**
     * @brief set global threshold for anomaly filtering
     */
    void set_glob_threshold(const double& l) { m_histogram.glob_threshold = l;}

    /*
     * @brief set bin counts in Histogram
     * @param c: vector of bin counts
     */
    void set_counts(const std::vector<int> & c) { m_histogram.counts = c; }

    /*
     * @brief set bin edges in Histogram
     * @param be: vector of bin edges
     */
    void set_bin_edges(const std::vector<double>& be) {m_histogram.bin_edges = be;}

    /*
     * @brief New bin counts in Histogram
     * @param count: bin count value
     */
    void add2counts(const int& count) {m_histogram.counts.push_back(count);}

    /*
     * @brief Update counts for a given index of bin in histogram
     * @param id: index of bin in Histogram
     * @param count: bin count value to update
     */
    void add2counts(const int& id, const int& count) {m_histogram.counts[id] += count;}

    /*
     * @brief New bin edges in histogram
     * @param bin_edge: vector of bin edges of histogram
     */
    void add2binedges(const double& bin_edge) {m_histogram.bin_edges.push_back(bin_edge);}

    /*
     * @brief get current value of global threshold from global histogram
     * @return global threshold
     */
    const double& get_threshold() const {return m_histogram.glob_threshold;}

    /**
     * @brief Get current vector of bin counts of Histogram
     * @return vector of bin counts
     */
    const std::vector<int>& counts() const {return m_histogram.counts;}

    /**
     * @brief Get current vector of bin edges of histogram
     * @return vector of bin edges
     */
    const std::vector<double>& bin_edges() const {return m_histogram.bin_edges;}

    /**
     * @brief Get the lower and upper edges of the given bin
     */
    inline std::pair<double,double> binEdges(const int bin) const{ return {m_histogram.bin_edges[bin],m_histogram.bin_edges[bin+1]}; }
    
    /**
     * @brief Get the count of a given bin
     */
    inline int binCount(const int bin) const{ return m_histogram.counts[bin]; }

    /**
     * @brief Get the current statistics as a JSON object
     */
    nlohmann::json get_json() const;

    /**
     * @brief Return the number of bins
     */
    size_t Nbin() const{ return counts().size(); }

    /**
     * @brief Enums for reporting bin index outside of histogram
     */
    enum { LeftOfHistogram = -1, RightOfHistogram = -2 };
    
    /**
     * @brief Get the bin containing value 'v'
     * @param v The value
     * @param tol The tolerance, as a fraction of the closest bin width,  that we allow values to be below the smallest bin or above the largest bin and still consider the value within that bin
     * @return Bin index if within the histogram, otherwise either LeftOfHistogram or RightOfHistogram
     */
    int getBin(const double v, const double tol = 0.) const;


    /**
     * @brief Generate a representative data set from the histogram assuming a bin is represented by its midpoint value
     *
     * @return Vector of values of a size equal to the sum of the bin counts
     */
    std::vector<double> unpack() const;


    /**
     * @brief Get the value represented by a bin; we use the midpoint
     */
    inline double binValue(const size_t i) const{ return binValue(i, bin_edges()); }


    /**
     * @brief Workspace for empiricalCDF function allows repeated calls to avoid recomputing the total sum
     */
    struct empiricalCDFworkspace{
      bool set;
      int sum;
      empiricalCDFworkspace(): set(false){}

      int getSum(const Histogram &h);
    };
 
    /**
     * @brief Compute the empirical CDF for a given value assuming bins are represented by bin_count counts of the midpoint value
     * @param value The value at to which the CDF is computed
     * @param workspace If non-null, workspace allows the preservation of the bin count sum between successive calls, reducing the cost
     */
    double empiricalCDF(const double value, empiricalCDFworkspace *workspace = nullptr) const;

    /**
     * @brief Return the sum of all the bin counts
     */
    int totalCount() const;

    /**
     * @brief Comparison operator
     */
    bool operator==(const Histogram &r) const{ return m_histogram == r.m_histogram; }

    /**
     * @brief Return a copy of the histogram but whose bin edges are negated and the bin order reversed
     */
    Histogram operator-() const;

    /**
     * @brief Compute the skewness of the distribution, assuming the bins are represented by their midpoint value
     */
    double skewness() const;

    /**
     * @brief Compute bin width based on Scott's rule
     * @param vals: vector of runtimes
     * @return computed bin width
     */
    static double scottBinWidth(const std::vector<double> & vals);

  private:
    Data m_histogram; /**< Histogram Data (bin counts, bin edges)*/

    /**
     * @brief Compute bin width based on Scott's rule
     * @param global_counts: bin counts in global histogram on pserver
     * @param global_edges: bin edges in global histogram on pserver
     * @param local_counts: bin counts in local histogram in AD module
     * @param local_edges: bin edges in local histogram in AD module
     * @return computed bin width
     */
    static double scottBinWidth(const std::vector<int> & global_counts, const std::vector<double> & global_edges, const std::vector<int> & local_counts, const std::vector<double> & local_edges);


    /**
     * @brief Get the value represented by a bin; we use the midpoint
     */
    static double binValue(const size_t i, const std::vector<double> & edges);
  };


  Histogram operator+(const Histogram& g, const Histogram& l);

  std::ostream & operator<<(std::ostream &os, const Histogram &h);
}
