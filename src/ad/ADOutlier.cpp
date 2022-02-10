#include "chimbuko/ad/ADOutlier.hpp"
#include "chimbuko/param/sstd_param.hpp"
#include "chimbuko/param/hbos_param.hpp"
#include "chimbuko/param/copod_param.hpp"
#include "chimbuko/message.hpp"
#include "chimbuko/verbose.hpp"
#include "chimbuko/util/error.hpp"
#include <mpi.h>
#include <nlohmann/json.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/empirical_cumulative_distribution_function.hpp>
#include <limits>

using namespace chimbuko;

/* ---------------------------------------------------------------------------
 * Implementation of ADOutlier class
 * --------------------------------------------------------------------------- */
ADOutlier::ADOutlier(OutlierStatistic stat)
  : m_execDataMap(nullptr), m_param(nullptr), m_use_ps(false), m_perf(nullptr), m_statistic(stat)
{
}

ADOutlier::~ADOutlier() {
    if (m_param) {
        delete m_param;
    }
}

ADOutlier *ADOutlier::set_algorithm(OutlierStatistic stat, const std::string & algorithm, const double & hbos_thres, const bool & glob_thres, const double & sstd_sigma) {

  if (algorithm == "sstd" || algorithm == "SSTD") {

    return new ADOutlierSSTD(stat, sstd_sigma);
  }
  else if (algorithm == "hbos" || algorithm == "HBOS") {
    return new ADOutlierHBOS(stat, hbos_thres, glob_thres);
  }
  else if (algorithm == "copod" || algorithm == "COPOD") {
    return new ADOutlierCOPOD(stat, hbos_thres);
  }
  else {
    return nullptr;
  }
}

void ADOutlier::linkNetworkClient(ADThreadNetClient *client){
  m_net_client = client;
  m_use_ps = (m_net_client != nullptr && m_net_client->use_ps());
}

double ADOutlier::getStatisticValue(const ExecData_t &e) const{
  switch(m_statistic){
  case ExclusiveRuntime:
    return e.get_exclusive();
  case InclusiveRuntime:
    return e.get_inclusive();
  default:
    throw std::runtime_error("Invalid statistic");
  }
}

std::pair<size_t,size_t> ADOutlier::sync_param(ParamInterface const* param)
{
  if (!m_use_ps) {
    verboseStream << "m_use_ps not USED!" << std::endl;
    m_param->update(param->serialize());
    return std::make_pair(0, 0);
  }
  else {
    Message msg;
    msg.set_info(m_net_client->get_client_rank(), m_net_client->get_server_rank(), MessageType::REQ_ADD, MessageKind::PARAMETERS);
    msg.set_msg(param->serialize(), false);
    size_t sent_sz = msg.size();

    m_net_client->send_and_receive(msg, msg);
    size_t recv_sz = msg.size();
    m_param->assign(msg.buf());
    return std::make_pair(sent_sz, recv_sz);
  }
}




/* ---------------------------------------------------------------------------
 * Implementation of ADOutlierSSTD class
 * --------------------------------------------------------------------------- */
ADOutlierSSTD::ADOutlierSSTD(OutlierStatistic stat, double sigma) : ADOutlier(stat), m_sigma(sigma) {
    m_param = new SstdParam();
}

ADOutlierSSTD::~ADOutlierSSTD() {
}

Anomalies ADOutlierSSTD::run(int step) {
  Anomalies outliers;
  if (m_execDataMap == nullptr) return outliers;

  //If using CUDA without precompiled kernels the first time a function is encountered takes much longer as it does a JIT compile
  //Python scripts also appear to take longer executing a function the first time
  //This is worked around by ignoring the first time a function is encountered (per device)
  //Set this environment variable to disable the workaround
  bool cuda_jit_workaround = true;
  if(const char* env_p = std::getenv("CHIMBUKO_DISABLE_CUDA_JIT_WORKAROUND")){
    cuda_jit_workaround = false;
  }

  //Generate the statistics based on this IO step
  SstdParam param;
  for (auto it : *m_execDataMap) { //loop over functions (key is function index)
    unsigned long func_id = it.first;
    for (auto itt : it.second) { //loop over events for that function
	if(itt->get_label() == 0){ //has not been analyzed previously
	//Update local counts of number of times encountered
	std::array<unsigned long, 4> fkey({itt->get_pid(), itt->get_rid(), itt->get_tid(), func_id});
	auto encounter_it = m_local_func_exec_count.find(fkey);
	if(encounter_it == m_local_func_exec_count.end())
	  encounter_it = m_local_func_exec_count.insert({fkey, 0}).first;
	else
	  encounter_it->second++;

	if(!cuda_jit_workaround || encounter_it->second > 0){ //ignore first encounter to avoid including CUDA JIT compiles in stats (later this should be done only for GPU kernels
	  param[func_id].push( this->getStatisticValue(*itt) );
	}
      }
    }
  }

  //Update temp runstats to include information collected previously (synchronizes with the parameter server if connected)
  PerfTimer timer;
  timer.start();
  std::pair<size_t, size_t> msgsz = sync_param(&param);

  if(m_perf != nullptr){
    m_perf->add("param_update_ms", timer.elapsed_ms());
    m_perf->add("param_sent_MB", (double)msgsz.first / 1000000.0); // MB
    m_perf->add("param_recv_MB", (double)msgsz.second / 1000000.0); // MB
  }

  //Run anomaly detection algorithm
  for (auto it : *m_execDataMap) { //loop over function index
    const unsigned long func_id = it.first;
    const unsigned long n = compute_outliers(outliers,func_id, it.second);
  }
  return outliers;
}


double ADOutlierSSTD::computeScore(CallListIterator_t ev, const SstdParam &stats) const{
  auto it = stats.get_runstats().find(ev->get_fid());
  if(it == stats.get_runstats().end()) fatal_error("Function not in stats!");
  double runtime = this->getStatisticValue(*ev);
  double mean = it->second.mean();
  double std_dev = it->second.stddev();
  if(std_dev == 0.) std_dev = 1e-10; //distribution throws an error if std.dev = 0

  //boost::math::normal_distribution<double> dist(mean, std_dev);
  //double cdf_val = boost::math::cdf(dist, runtime); // P( X <= x ) for random variable X
  //double score = std::min(cdf_val, 1-cdf_val); //two-tailed

  //Using the CDF gives scores ~0 for basically any global outlier
  //Instead we will use the difference in runtime compared to the avg in units of the standard deviation
  double score = fabs( runtime - mean ) / std_dev;

  verboseStream << "ADOutlierSSTD::computeScore " << ev->get_funcname() << " runtime " << runtime << " mean " << mean << " std.dev " << std_dev << " -> score " << score << std::endl;
  return score;
}


unsigned long ADOutlierSSTD::compute_outliers(Anomalies &outliers,
					      const unsigned long func_id,
					      std::vector<CallListIterator_t>& data){
  verboseStream << "Finding outliers in events for func " << func_id << std::endl;


  SstdParam& param = *(SstdParam*)m_param;
  if (param[func_id].count() < 2){
    verboseStream << "Less than 2 events in stats associated with that func, stats not complete" << std::endl;
    return 0;
  }
  unsigned long n_outliers = 0;

  const double mean = param[func_id].mean();
  const double std = param[func_id].stddev();

  const double thr_hi = mean + m_sigma * std;
  const double thr_lo = mean - m_sigma * std;

  for (auto itt : data) {
    if(itt->get_label() == 0){ //only label new events
      const double runtime = this->getStatisticValue(*itt);
      int label = (thr_lo > runtime || thr_hi < runtime) ? -1: 1;
      itt->set_label(label);
      itt->set_outlier_score(computeScore(itt, param));

      if (label == -1) {
	verboseStream << "!!!!!!!Detected outlier on func id " << func_id << " (" << itt->get_funcname() << ") on thread " << itt->get_tid()
		      << " runtime " << runtime << " mean " << mean << " std " << std << std::endl;
	n_outliers += 1;
  	std::vector<double> sstd_stats{thr_hi, thr_lo, mean, std};
	outliers.insert(itt, Anomalies::EventType::Outlier, sstd_stats); //insert into data structure containing captured anomalies
      }else{
	//Capture maximum of one normal execution per io step
	if(outliers.nFuncEvents(func_id, Anomalies::EventType::Normal) == 0){
	  verboseStream << "Detected normal event on func id " << func_id << " (" << itt->get_funcname() << ") on thread " << itt->get_tid()
			<< " runtime " << runtime << " mean " << mean << " std " << std << std::endl;

	  outliers.insert(itt, Anomalies::EventType::Normal);
	}
      }
    }
  }

  return n_outliers;
}




/* ---------------------------------------------------------------------------
 * Implementation of ADOutlierHBOS class
 * --------------------------------------------------------------------------- */
ADOutlierHBOS::ADOutlierHBOS(OutlierStatistic stat, double threshold, bool use_global_threshold) : ADOutlier(stat), m_alpha(78.88e-32), m_threshold(threshold), m_use_global_threshold(use_global_threshold) {
    m_param = new HbosParam();
}

ADOutlierHBOS::~ADOutlierHBOS() {
  if (m_param)
    m_param->clear();
}

Anomalies ADOutlierHBOS::run(int step) {
  Anomalies outliers;
  if (m_execDataMap == nullptr) return outliers;

  //Generate the statistics based on this IO step
  HbosParam param;
  for (auto it : *m_execDataMap) { //loop over functions (key is function index)
    unsigned long func_id = it.first;
    Histogram &hist = param[func_id];
    std::vector<double> runtimes;
    for (auto itt : it.second) { //loop over events for that function
      if (itt->get_label() == 0)
	runtimes.push_back(this->getStatisticValue(*itt));
    }
    verboseStream << "Function " << func_id << " has " << runtimes.size() << " unlabeled data points of " << it.second.size() << std::endl;

    if (runtimes.size() > 0) {
      verboseStream << "Creating local histogram for func " << func_id << " for " << runtimes.size() << " data points" << std::endl;
      const int r = hist.create_histogram(runtimes);
      if (r < 0) {
	recoverable_error(std::string("AD: Func_ID does not exist"));
	continue;
      }
    }
    verboseStream << "Function " << func_id << " generated histogram has " << hist.counts().size() << " bins:" << std::endl;
    verboseStream << hist << std::endl;
  }

  //Update temp runstats to include information collected previously (synchronizes with the parameter server if connected)
  PerfTimer timer;
  timer.start();
  std::pair<size_t, size_t> msgsz = sync_param(&param);

  if(m_perf != nullptr){
    m_perf->add("param_update_ms", timer.elapsed_ms());
    m_perf->add("param_sent_MB", (double)msgsz.first / 1000000.0); // MB
    m_perf->add("param_recv_MB", (double)msgsz.second / 1000000.0); // MB
  }

  //Run anomaly detection algorithm
  for (auto it : *m_execDataMap) { //loop over function index
    const unsigned long func_id = it.first;
    const unsigned long n = compute_outliers(outliers,func_id, it.second);
  }

  return outliers;
}

unsigned long ADOutlierHBOS::compute_outliers(Anomalies &outliers,
					      const unsigned long func_id,
					      std::vector<CallListIterator_t>& data){

  verboseStream << "Finding outliers in events for func " << func_id << std::endl;

  //Check if there are any unlabeled data points first before we go through the effort of precomputing scores
  // size_t n_unlabeled = 0;
  // for (auto itt : data)
  //   if(itt->get_label() == 0) ++n_unlabeled;

  // if(n_unlabeled == 0){
  //   verboseStream << "Function has no unlabeled events, skipping" << std::endl;
  //   return 0;
  // }


  HbosParam& param = *(HbosParam*)m_param;
  Histogram &hist = param[func_id];

  unsigned long n_outliers = 0;

  auto const & bin_counts = hist.counts();
  auto const & bin_edges = hist.bin_edges();

  size_t nbin = bin_counts.size();
  size_t nedge = bin_edges.size();

  verboseStream << "Number of bins " << nbin << std::endl;

  //Check that the histogram contains bins
  if(nbin == 0){
    //This should only happen in the case where the histogram had no data for this function prior to this IO step and this IO step contains no unlabeled data points
    size_t n_unlabeled=0;
    for (auto itt : data)
      if(itt->get_label() == 0) ++n_unlabeled;

    if(n_unlabeled != 0){ 
      fatal_error("Logic bomb: Histogram has 0 bins but dataset contains "+std::to_string(n_unlabeled)+" unlabeled data for this function!");
    }else{
      verboseStream << "No bins and no unlabeled data points, returning" << std::endl;
      return 0;
    }
  }

  //For a histogram that has bins, the number of edges should be nbin+1
  if(nedge != nbin+1) fatal_error("Number of histogram edges is not 1 larger than the number of bins: #bins "+std::to_string(nbin)+" #edges "+std::to_string(nedge));

  //Bounds of the range of possible scores
  const double max_possible_score = -1 * log2(0.0 + m_alpha); //-log2(78.88e-32) ~ 100.0 by default (i.e. the theoretical max score)
  const double min_possible_score = -1 * log2(1.0 + m_alpha); //-log2(1+78.88e-32) ~ 0.0 by default (i.e. the theoretical max score)

  //Find the smallest and largest scores in the histogram (excluding empty bins)
  double min_score = max_possible_score;
  double max_score = min_possible_score;

  //Compute scores
  double tot_runtimes = std::accumulate(bin_counts.begin(), bin_counts.end(), 0.0);
  std::vector<double> out_scores_i(nbin);

  verboseStream << "out_scores_i: " << std::endl;
  for(int i=0; i < nbin; i++){
    int count = bin_counts[i];
    double prob = count / tot_runtimes;
    double score = -1 * log2(prob + m_alpha);
    out_scores_i[i] = score;
    verboseStream << "Bin " << i << ", Range " << bin_edges[i] << "-" << bin_edges[i+1] << ", Count: " << count << ", Probability: " << prob << ", score: "<< score << std::endl;
    if(prob > 0) {
      min_score = std::min(min_score,score);
      max_score = std::max(max_score,score);
    }
  }
  verboseStream << std::endl;
  verboseStream << "min_score = " << min_score << std::endl;
  verboseStream << "max_score = " << max_score << std::endl;

  //Compute threshold as a fraction of the range of scores in the histogram
  verboseStream << "Global threshold before comparison with local threshold =  " << hist.get_threshold() << std::endl;
  double l_threshold = min_score + (m_threshold * (max_score - min_score));
  verboseStream << "Local threshold " << l_threshold << std::endl;
  if(m_use_global_threshold) {
    if(l_threshold < hist.get_threshold()) {
      verboseStream << "Using global threshold as it is more stringent" << std::endl;
      l_threshold = hist.get_threshold();
    } else {
      verboseStream << "Using local threshold as it is more stringent" << std::endl;
      hist.set_glob_threshold(l_threshold); 
    }
  }

  //Compute HBOS based score for each datapoint
  const double bin_width = hist.bin_edges().at(1) - hist.bin_edges().at(0);
  verboseStream << "Bin width: " << bin_width << std::endl;

  int top_out = 0;
  for (auto itt : data) {
    if (itt->get_label() == 0) {

      const double runtime_i = this->getStatisticValue(*itt); //runtimes.push_back(this->getStatisticValue(*itt));
      double ad_score;

      verboseStream << "Locating " << itt->get_json().dump() << std::endl;
      const int bin_ind = hist.getBin(runtime_i, 0.05); //allow events within 5% of the bin width away from the histogram edges to be included in the first/last bin
      verboseStream << "bin_ind: " << bin_ind << " for runtime_i: " << runtime_i << ", where bin_edges Size:" << nedge << " & num_bins: "<< nbin << std::endl;

      if( bin_ind == Histogram::LeftOfHistogram || bin_ind == Histogram::RightOfHistogram){
	//Sample (datapoint) lies outside of the histogram
	verboseStream << runtime_i << " is on " << (bin_ind == Histogram::LeftOfHistogram ? "left" : "right")  << " of histogram and an outlier" << std::endl;
	ad_score = max_possible_score;
	verboseStream << "ad_score(max_possible_score): " << ad_score << std::endl;
      }else{
	//Sample is within the histogram
	verboseStream << runtime_i << " maybe be an outlier" << std::endl;
	ad_score = out_scores_i[bin_ind];
	verboseStream << "ad_score(else): " << ad_score << ", bin_ind: " << bin_ind  << ", num_bins: " << nbin << ", out_scores_i size: " << out_scores_i.size() << std::endl;
      }

      //handle when ad_score = 0
      //This is valid when there is only one bin as the probability is 1 and log(1) = 0
      //Note that the total number of bins can be > 1 providing the other bins have 0 counts
      if (ad_score <= 0 ){
	int nbin_nonzero = 0;
	for(int c : hist.counts())
	  if(c>0) ++nbin_nonzero;
	if(nbin_nonzero != 1){
	  fatal_error("ad_score <= 0 but #bins with non zero count, "+std::to_string(nbin_nonzero)+" is not 1");
	}
      }


      itt->set_outlier_score(ad_score);
      verboseStream << "ad_score: " << ad_score << ", l_threshold: " << l_threshold << std::endl;

      //Compare the ad_score with the threshold
      if (ad_score >= l_threshold) {
	itt->set_label(-1);
	verboseStream << "!!!!!!!Detected outlier on func id " << func_id << " (" << itt->get_funcname() << ") on thread " << itt->get_tid() << " runtime " << runtime_i << " score " << ad_score << " (threshold " << l_threshold << ")" << std::endl;
	outliers.insert(itt, Anomalies::EventType::Outlier, runtime_i, ad_score, l_threshold); //insert into data structure containing captured anomalies
	n_outliers += 1;
      }else {
        //Capture maximum of one normal execution per io step
        itt->set_label(1);
        if(outliers.nFuncEvents(func_id, Anomalies::EventType::Normal) == 0) {
	  verboseStream << "Detected normal event on func id " << func_id << " (" << itt->get_funcname() << ") on thread " << itt->get_tid() << " runtime " << runtime_i << " score " << ad_score << " (threshold " << l_threshold << ")" << std::endl;
	  outliers.insert(itt, Anomalies::EventType::Normal);
        }
      }

    }//if unlabeled point
  } //loop over data points

  return n_outliers;
}


int ADOutlierHBOS::np_digitize_get_bin_inds(const double& X, const std::vector<double>& bin_edges) {


  if(bin_edges.size() < 2){ // If only one bin exists in the Histogram
    return 0;
  }


  for(int j=1; j < bin_edges.size(); j++){

    if(X <= bin_edges.at(j)){

      return (j-1);
    }
  }

  const int ret_val = bin_edges.size();

  return  ret_val;
}



/* ---------------------------------------------------------------------------
 * Implementation of ADOutlierCOPOD class
 * --------------------------------------------------------------------------- */
ADOutlierCOPOD::ADOutlierCOPOD(OutlierStatistic stat, double threshold, bool use_global_threshold) : ADOutlier(stat), m_alpha(78.88e-32), m_threshold(threshold), m_use_global_threshold(use_global_threshold) {
    m_param = new CopodParam();
}

ADOutlierCOPOD::~ADOutlierCOPOD() {
  if (m_param)
    m_param->clear();
}

Anomalies ADOutlierCOPOD::run(int step) {
  Anomalies outliers;
  if (m_execDataMap == nullptr) return outliers;

  //If using CUDA without precompiled kernels the first time a function is encountered takes much longer as it does a JIT compile
  //Python scripts also appear to take longer executing a function the first time
  //This is worked around by ignoring the first time a function is encountered (per device)
  //Set this environment variable to disable the workaround
  bool cuda_jit_workaround = true;
  if(const char* env_p = std::getenv("CHIMBUKO_DISABLE_CUDA_JIT_WORKAROUND")){
    cuda_jit_workaround = false;
  }

  //Generate the statistics based on this IO step
  CopodParam param;
  CopodParam& g = *(CopodParam*)m_param;
  for (auto it : *m_execDataMap) { //loop over functions (key is function index)
    unsigned long func_id = it.first;
    Histogram &hist = param[func_id];
    std::vector<double> runtimes;
    for (auto itt : it.second) { //loop over events for that function
      if (itt->get_label() == 0) {
        //Update local counts of number of times encountered
        std::array<unsigned long, 4> fkey({itt->get_pid(), itt->get_rid(), itt->get_tid(), func_id});
        auto encounter_it = m_local_func_exec_count.find(fkey);
        if(encounter_it == m_local_func_exec_count.end())
  	encounter_it = m_local_func_exec_count.insert({fkey, 0}).first;
        else
  	encounter_it->second++;

        if(!cuda_jit_workaround || encounter_it->second > 0){ //ignore first encounter to avoid including CUDA JIT compiles in stats (later this should be done only for GPU kernels

           runtimes.push_back(this->getStatisticValue(*itt));
        }
      }
    }
    if (runtimes.size() > 0) {
      if (!g.find(func_id)) { // If func_id does not exist
        const int r = hist.create_histogram(runtimes);
        if (r < 0) {
		recoverable_error(std::string("AD: Func_ID does not exist "));
		continue;
	}
      }
      else { //merge with exisiting func_id, not overwrite

        const int r = hist.merge_histograms(g[func_id], runtimes);
	if (r < 0) {
		verboseStream << "AD: Merging reset " << std::endl;
		continue;
	}
      }
    }
    else { 
      ///recoverable_error(std::string("AD: Zero function runtimes "));
	    continue;
    }
    verboseStream << "Size of runtimes: " << runtimes.size() << ", func_id: " << func_id << std::endl;
    
    //calculate skewness of runtimes for func_id
    const double mu = std::accumulate(runtimes.begin(), runtimes.end(), 0.0) / runtimes.size();

    std::vector<double> diff = std::vector<double>(runtimes.size());
    std::transform(runtimes.begin(), runtimes.end(), diff.begin(), [mu](double x) {return x - mu;});
    const double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
    const double stdev = std::sqrt(sq_sum / runtimes.size());

    double summ = 0;
    for (int i=0; i<runtimes.size(); i++) {
	summ += std::pow((runtimes.at(i) - mu), 3);
    }

    const double abs_skewness = summ / ((runtimes.size() - 1) * std::pow(stdev,3));
    m_skewness[func_id] = (abs_skewness < 0) ? -1 : (abs_skewness > 0) ? 1 : 0;

  }

  //Update temp runstats to include information collected previously (synchronizes with the parameter server if connected)
  PerfTimer timer;
  timer.start();
  std::pair<size_t, size_t> msgsz = sync_param(&param);

  if(m_perf != nullptr){
    m_perf->add("param_update_ms", timer.elapsed_ms());
    m_perf->add("param_sent_MB", (double)msgsz.first / 1000000.0); // MB
    m_perf->add("param_recv_MB", (double)msgsz.second / 1000000.0); // MB
  }

  //Run anomaly detection algorithm
  for (auto it : *m_execDataMap) { //loop over function index
    const unsigned long func_id = it.first;
    const unsigned long n = compute_outliers(outliers,func_id, it.second);
  }

  return outliers;
}

unsigned long ADOutlierCOPOD::compute_outliers(Anomalies &outliers,
					      const unsigned long func_id,
					      std::vector<CallListIterator_t>& data){

  verboseStream << "Finding outliers in events for func " << func_id << std::endl;
  verboseStream << "data Size: " << data.size() << std::endl;

  CopodParam& param = *(CopodParam*)m_param;
  Histogram &hist = param[func_id];

  unsigned long n_outliers = 0;

  //probability of runtime counts
  //std::vector<double> prob_counts = std::vector<double>(param[func_id].counts().size(), 0.0);
  double tot_runtimes = std::accumulate(hist.counts().begin(), hist.counts().end(), 0.0);
  int lowest_count = std::numeric_limits<int>::max();

  if (tot_runtimes <= 0 ) {
	  return n_outliers;
  }
  std::vector<double> recon_p_runtimes = std::vector<double>(tot_runtimes, 0.0);
  std::vector<double> recon_n_runtimes = std::vector<double>(tot_runtimes, 0.0);
  int recon_idx = 0;
  verboseStream << "Unwrapping Merged Histogram. Size: " << hist.counts().size() << std::endl;
  for(int i=0; i < hist.counts().size(); i++){
    int count = hist.counts().at(i);
    verboseStream << "Count: " << count << ", Value: " << hist.bin_edges().at(i) << std::endl;
    for(int j=0; j<count; j++){

      recon_p_runtimes.at(recon_idx) = hist.bin_edges().at(i);
      recon_n_runtimes.at(recon_idx) = -1 * hist.bin_edges().at(i);
      verboseStream << "recon_idx: " << recon_idx << std::endl;
      verboseStream << "recon_p_runtimes.at(recon_idx): " << recon_p_runtimes.at(recon_idx) << ", recon_n_runtimes.at(recon_idx): " << recon_n_runtimes.at(recon_idx) << std::endl;
      recon_idx++;
    }

    if (count < lowest_count)
      lowest_count = count;
  }


  std::vector<double> func_p_ecdf = empiricalCDF(recon_p_runtimes, true);
  std::vector<double> func_n_ecdf = empiricalCDF(recon_n_runtimes, true);
  
  verboseStream << "Size of empiricalCDF(recon_p_runtimes): " << func_p_ecdf.size() << std::endl;
  verboseStream << "Size of empiricalCDF(recon_n_runtimes): " << func_n_ecdf.size() << std::endl;

  std::vector<double> mean_pn_ecdf = std::vector<double>(func_p_ecdf.size(), 0.0);
  verboseStream << "Size of mean_pn_ecdf: " << mean_pn_ecdf.size() << ", func_id: " << func_id << std::endl;

  for(int i=0; i < mean_pn_ecdf.size(); i++){
    mean_pn_ecdf.at(i) = (func_p_ecdf.at(i) + func_n_ecdf.at(i)) / 2.0;
    verboseStream << "mean_pn_ecdf.at(i): " << mean_pn_ecdf.at(i) << ", func_p_ecdf.at(i): " << func_p_ecdf.at(i) << ", func_n_ecdf.at(i): " << func_n_ecdf.at(i) << std::endl;
  }

  //use skewness
  std::vector<double> skewness_arr = std::vector<double>(func_p_ecdf.size(), 0.0);
  const int p_sign = (m_skewness[func_id] - 1) < 0 ? -1 : (m_skewness[func_id] - 1) > 0 ? 1 : 0;
  const int n_sign = (m_skewness[func_id] + 1) < 0 ? -1 : (m_skewness[func_id] + 1) > 0 ? 1 : 0;
  
  for (int i = 0; i< func_p_ecdf.size(); i++) {
	skewness_arr.at(i) = (func_p_ecdf.at(i) * -1 * p_sign) + (func_n_ecdf.at(i) * n_sign);
  	verboseStream << "skewness_arr.at(" << i << "): " << skewness_arr.at(i) << std::endl;
  }

  std::vector<double> final_comp = std::vector<double>(skewness_arr.size(), 0.0);
  for (int i = 0; i < skewness_arr.size(); i++) {
	final_comp.at(i) = std::max(skewness_arr.at(i), mean_pn_ecdf.at(i));
  }


  //Create COPOD score vector
  std::vector<double> out_scores_i = std::vector<double>(final_comp.size(), 0.0);
  verboseStream << "m_alpha: " << m_alpha << std::endl;

  double min_score = -1 * log2(0.0 + m_alpha);
  double max_score = log2(1.0 + m_alpha) - min_score;
  verboseStream << "Initializaing min_score: " << min_score << ", max_score: " << max_score <<std::endl;
  verboseStream << "out_scores_i: " << std::endl;
  for(int i=0; i < final_comp.size(); i++){
    double l = -1 * log2(final_comp.at(i) + m_alpha);
    out_scores_i.at(i) = l;
    verboseStream << "Final_comp at " << i << ": " << final_comp.at(i) << ", score: "<< l << std::endl;
    //if(prob_counts.at(i) > 0) {
      if(l < min_score){
        min_score = l;
      }
      if(l > max_score){
        max_score = l;
      }
    //}
  }
  verboseStream << std::endl;
  verboseStream << "out_score_i size: " << out_scores_i.size() << std::endl;
  verboseStream << "min_score = " << min_score << std::endl;
  verboseStream << "max_score = " << max_score << std::endl;

  if (out_scores_i.size() <= 0) {return 0;}

  //compute threshold
  verboseStream << "Global threshold before comparison with local threshold =  " << hist.get_threshold() << std::endl;
  double l_threshold = (max_score < 0) ? (-1 * m_threshold * (max_score - min_score)) : min_score + (m_threshold * (max_score - min_score));
  verboseStream << "l_threshold computed: " << l_threshold << std::endl;
  if(m_use_global_threshold) {
    if(l_threshold < hist.get_threshold() && hist.get_threshold() > (-1 * log2(1.00001))) {
      l_threshold = hist.get_threshold();
    } else {
      hist.set_glob_threshold(l_threshold); //.get_histogram().glob_threshold = l_threshold;
      //std::pair<size_t, size_t> msgsz_thres_update = sync_param(&param);
    }
  }

  //Compute COPOD based score for each datapoint

  int top_out = 0;
  int running_idx = 0;
  for (auto itt : data) {
    if (itt->get_label() == 0) {

      const double runtime_i = this->getStatisticValue(*itt); //runtimes.push_back(this->getStatisticValue(*itt));
      double ad_score;
      
      if (running_idx < final_comp.size()) {
      	verboseStream << "final_comp.at(" << running_idx << "): " << final_comp.at(running_idx) << ", func_id: " << func_id << ", runtime: " << runtime_i << std::endl;
     
        if (out_scores_i.at(running_idx) > l_threshold) //(final_comp.at(running_idx) > 0) // < 0.9)
	        ad_score = l_threshold + 1;
        else
	        ad_score = l_threshold - 1;
        
	running_idx++;
      }
      else {
	verboseStream << "AD: COPOD: runtime Index" << std::endl;
        continue;
      }

      //handle when ad_score = 0
      if (ad_score <= 0)
        ad_score = -1 * log2((lowest_count / tot_runtimes) + m_alpha);

      itt->set_outlier_score(ad_score);
      verboseStream << "ad_score: " << ad_score << ", l_threshold: " << l_threshold << std::endl;

      //Compare the ad_score with the threshold
      if (ad_score >= l_threshold) {

          itt->set_label(-1);
          verboseStream << "!!!!!!!Detected outlier on func id " << func_id << " (" << itt->get_funcname() << ") on thread " << itt->get_tid() << " runtime " << runtime_i << std::endl;
          outliers.insert(itt, Anomalies::EventType::Outlier, runtime_i, ad_score, l_threshold); //insert into data structure containing captured anomalies
          n_outliers += 1;

      }
      else {
        //Capture maximum of one normal execution per io step
        itt->set_label(1);
        if(outliers.nFuncEvents(func_id, Anomalies::EventType::Normal) == 0) {
      	   verboseStream << "Detected normal event on func id " << func_id << " (" << itt->get_funcname() << ") on thread " << itt->get_tid() << " runtime " << runtime_i << std::endl;
      	   outliers.insert(itt, Anomalies::EventType::Normal);
        }

      }
    }
  }

  return n_outliers;
}


int ADOutlierCOPOD::np_digitize_get_bin_inds(const double& X, const std::vector<double>& bin_edges) {


  if(bin_edges.size() < 2){ // If only one bin exists in the Histogram
    return 0;
  }


  for(int j=1; j < bin_edges.size(); j++){

    if(X <= bin_edges.at(j)){

      return (j-1);
    }
  }

  const int ret_val = bin_edges.size();

  return  ret_val;
}

std::vector<double> ADOutlierCOPOD::empiricalCDF(const std::vector<double>& runtimes, const bool sorted) {

  std::vector<double> tmp_runtimes = runtimes;
  auto ecdf = boost::math::empirical_cumulative_distribution_function(std::move(tmp_runtimes));
  std::vector<double> result_ecdf = std::vector<double>(runtimes.size(), 0.0);
  for(int i=0; i < runtimes.size(); i++) {
    result_ecdf.at(i) = ecdf(runtimes.at(i));
  }

  return result_ecdf;

}
