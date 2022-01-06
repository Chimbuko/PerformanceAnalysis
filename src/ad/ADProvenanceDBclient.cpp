#include<atomic>
#include<chrono>
#include<thread>
#include<yokan/cxx/admin.hpp>
#include<chimbuko/ad/ADProvenanceDBclient.hpp>
#include<chimbuko/verbose.hpp>
#include<chimbuko/util/string.hpp>
#include<chimbuko/provdb/setup.hpp>
#include<chimbuko/util/error.hpp>

#ifdef ENABLE_PROVDB

using namespace chimbuko;

//Enable asynchronous comms; if disabled async calls will fall back to blocking calls
#define PROVDB_ENABLE_ASYNC


void AnomalousSendManager::purge(){
  auto it = outstanding.begin();
  while(it != outstanding.end()){
    if(it->test()){
      it->wait();
      it = outstanding.erase(it);
    }else{
      ++it;
    }
  }

  int nremaining = outstanding.size();
  int ncomplete_remaining = 0;
  for(auto &e: outstanding)
    if(e.test()) ++ncomplete_remaining;
  
  if(ncomplete_remaining > 0)
    recoverable_error("After purging complete sends from the front of the queue, the queue still contains " + std::to_string(ncomplete_remaining) + "/" + std::to_string(nremaining) + " completed but unremoved ids");
}

thallium::eventual<void> & AnomalousSendManager::getNewRequest(){
  purge();
  outstanding.emplace_back();
  return outstanding.back();
}

void AnomalousSendManager::waitAll(){
  //Have another thread produce heartbeat information so we can know if the AD gets stuck waiting to flush
  std::atomic<bool> ready(false); 
  int heartbeat_freq = 20;
  std::thread heartbeat([heartbeat_freq, &ready]{
			  typedef std::chrono::high_resolution_clock Clock;
			  typedef std::chrono::seconds Sec;
			  Clock::time_point start = Clock::now();
			  while(!ready.load(std::memory_order_relaxed)){
			    int sec = std::chrono::duration_cast<Sec>(Clock::now() - start).count();
			    if(sec >= heartbeat_freq && sec % heartbeat_freq == 0) //complain every heartbeat_freq seconds
			      std::cout << "AnomalousSendManager::waitAll still waiting for queue flush after " << sec << "s" << std::endl;
			    std::this_thread::sleep_for(std::chrono::seconds(1));
			  }
			});	    
  
  while(!outstanding.empty()){ //flush the queue
    outstanding.front().wait();
    outstanding.pop_front();
  }
  ready.store(true, std::memory_order_relaxed);
  heartbeat.join();
}  

size_t AnomalousSendManager::getNoutstanding(){
  purge();
  return outstanding.size();
}

AnomalousSendManager::~AnomalousSendManager(){
  waitAll();
  verboseStream << "AnomalousSendManager exiting" << std::endl;
}

ADProvenanceDBclient::~ADProvenanceDBclient(){ 
  disconnect();
  verboseStream << "ADProvenanceDBclient exiting" << std::endl;
}

yokan::Collection & ADProvenanceDBclient::getCollection(const ProvenanceDataType type){ 
  switch(type){
  case ProvenanceDataType::AnomalyData:
    return *m_coll_anomalies;
  case ProvenanceDataType::Metadata:
    return *m_coll_metadata;
  case ProvenanceDataType::NormalExecData:
    return *m_coll_normalexecs;
  default:
    throw std::runtime_error("Invalid type");
  }
}

const yokan::Collection & ADProvenanceDBclient::getCollection(const ProvenanceDataType type) const{ 
  return const_cast<ADProvenanceDBclient*>(this)->getCollection(type);
}

static void delete_rpc(thallium::remote_procedure* &rpc){
  delete rpc; rpc = nullptr;
}

ADProvenanceDBclient::ADProvenanceDBclient(int rank): m_is_connected(false), m_rank(rank), m_client_hello(nullptr), m_client_goodbye(nullptr), m_stats(nullptr), m_stop_server(nullptr), m_perform_handshake(true)
#ifdef ENABLE_MARGO_STATE_DUMP
    , m_margo_dump(nullptr)
#endif
    {}


void ADProvenanceDBclient::disconnect(){
  if(m_is_connected){
    verboseStream << "ADProvenanceDBclient disconnecting" << std::endl;
    verboseStream << "ADProvenanceDBclient is waiting for outstanding calls to complete" << std::endl;

    PerfTimer timer;
    anom_send_man.waitAll();
    if(m_stats) m_stats->add("provdb_client_disconnect_wait_all_ms", timer.elapsed_ms());

    if(m_perform_handshake){
      verboseStream << "ADProvenanceDBclient de-registering with server" << std::endl;
      m_client_goodbye->on(m_server)(m_rank);    
      verboseStream << "ADProvenanceDBclient deleting handshake RPCs" << std::endl;
      delete_rpc(m_client_hello);
      delete_rpc(m_client_goodbye);
    }
    delete_rpc(m_stop_server);
#ifdef ENABLE_MARGO_STATE_DUMP
    delete_rpc(m_margo_dump);
#endif
    m_is_connected = false;
    verboseStream << "ADProvenanceDBclient disconnected" << std::endl;
  }
}


yokan::Collection* openCollection(yokan::Database &db, const std::string &coll){
  if(!db.collectionExists(coll.c_str())) fatal_error("Collection " + coll + " does not exist on server");
  return new yokan::Collection(coll.c_str(), db);
}


void ADProvenanceDBclient::connect(const std::string &addr, const std::string &db_name, const int provider_idx){
  try{
    //Reset the protocol if necessary
    std::string protocol = ADProvenanceDBengine::getProtocolFromAddress(addr);
    if(ADProvenanceDBengine::getProtocol().first != protocol){
      int mode = ADProvenanceDBengine::getProtocol().second;
      verboseStream << "DB client reinitializing engine with protocol \"" << protocol << "\"" << std::endl;
      ADProvenanceDBengine::finalize();
      ADProvenanceDBengine::setProtocol(protocol,mode);      
    }      

    thallium::engine &eng = ADProvenanceDBengine::getEngine();

    m_client = yokan::Client(eng.get_margo_instance());
    verboseStream << "DB client rank " << m_rank << " connecting to database " << db_name << " on address " << addr << " and provider idx " << provider_idx << std::endl;

    //Lookup the address
    hg_addr_t addr_obj = HG_ADDR_NULL;
    hg_return_t hret = margo_addr_lookup(eng.get_margo_instance(), addr.c_str(), &addr_obj);
    if(hret != HG_SUCCESS) fatal_error("Could not look up address " + addr);
    
    {
      //Have another thread produce heartbeat information so we can know if the AD gets stuck waiting to connect to the provDB
      std::atomic<bool> ready(false); 
      int rank = m_rank;
      int heartbeat_freq = 20;
      std::thread heartbeat([heartbeat_freq, rank, &ready]{
	  typedef std::chrono::high_resolution_clock Clock;
	  typedef std::chrono::seconds Sec;
	  Clock::time_point start = Clock::now();
	  while(!ready.load(std::memory_order_relaxed)){
	    int sec = std::chrono::duration_cast<Sec>(Clock::now() - start).count();
	    if(sec >= heartbeat_freq && sec % heartbeat_freq == 0) //complain every heartbeat_freq seconds
	      std::cout << "DB client rank " << rank << " still waiting for provDB connection after " << sec << "s" << std::endl;
	    std::this_thread::sleep_for(std::chrono::seconds(1));
	  }
	});	    

      try{
	//Use a temporary Admin object to look up the database uid
	// yokan::Admin admin(eng.get_margo_instance());
	// std::vector<yk_database_id_t> databases = admin.listDatabases(addr_obj, provider_idx, "");
	// if(databases.size() != 1) fatal_error("Expect 1 database on the provider, got " + anyToStr(databases.size()) );      
	// m_database = m_client.makeDatabaseHandle(addr_obj, provider_idx, databases[0]);
	// margo_addr_free(eng.get_margo_instance(),addr_obj);

	verboseStream << "DB client attaching to database " << db_name << std::endl;
	m_database = m_client.findDatabaseByName(addr_obj, provider_idx, db_name.c_str());
      }catch(std::exception &e){
	ready.store(true, std::memory_order_relaxed);
	heartbeat.join();
	fatal_error(std::string("Caught exception: ") + e.what());
      }

      ready.store(true, std::memory_order_relaxed);
      heartbeat.join();
    }
    verboseStream << "DB client opening anomaly collection" << std::endl;
    m_coll_anomalies.reset( openCollection(m_database,"anomalies") );
    verboseStream << "DB client opening metadata collection" << std::endl;
    m_coll_metadata.reset( openCollection(m_database,"metadata") );
    verboseStream << "DB client opening normal execution collection" << std::endl;
    m_coll_normalexecs.reset( openCollection(m_database,"normalexecs") );

    m_server = eng.lookup(addr);

    if(m_perform_handshake){
      verboseStream << "DB client registering RPCs and handshaking with provDB" << std::endl;
      m_client_hello = new thallium::remote_procedure(eng.define("client_hello").disable_response());
      m_client_goodbye = new thallium::remote_procedure(eng.define("client_goodbye").disable_response());
      m_client_hello->on(m_server)(m_rank);
    }      

    m_stop_server = new thallium::remote_procedure(eng.define("stop_server").disable_response());
#ifdef ENABLE_MARGO_STATE_DUMP
    m_margo_dump = new thallium::remote_procedure(eng.define("margo_dump").disable_response());
#endif
    m_server_addr = addr;
    m_is_connected = true;
    verboseStream << "DB client rank " << m_rank << " connected successfully to database" << std::endl;

  }catch(const yokan::Exception& ex) {
    throw std::runtime_error(std::string("Provenance DB client could not connect due to exception: ") + ex.what());
  }
}



void ADProvenanceDBclient::connectSingleServer(const std::string &addr, const int nshards){
  ProvDBsetup setup(nshards, 1);

  //Assign shards round-robin
  int shard = m_rank % nshards;      
  std::string db_name = setup.getShardDBname(shard);
  int provider = setup.getShardProviderIndex(shard);
  
  connect(addr, db_name, provider);
}

void ADProvenanceDBclient::connectMultiServer(const std::string &addr_file_dir, const int nshards, const int ninstances){
  ProvDBsetup setup(nshards, ninstances);

  //Assign shards round-robin
  int shard = m_rank % nshards;
  int instance = setup.getShardInstance(shard); //which server instance
  std::string addr = setup.getInstanceAddress(instance, addr_file_dir);
  std::string db_name = setup.getShardDBname(shard);
  int provider = setup.getShardProviderIndex(shard);
  
  connect(addr, db_name, provider);
}

void ADProvenanceDBclient::connectMultiServerShardAssign(const std::string &addr_file_dir, const int nshards, const int ninstances, const std::string &shard_assign_file){
  ProvDBsetup setup(nshards, ninstances);

  std::ifstream f(shard_assign_file);
  if(f.fail()) throw std::runtime_error("Could not open shard assignment file " + shard_assign_file + "\n");
  int shard = -1;
  for(int i=0;i<=m_rank;i++){
    f >> shard;
    if(f.fail()) throw std::runtime_error("Failed to reach assignment for rank " + std::to_string(m_rank) + "\n");
  }
  f.close();

  int instance = setup.getShardInstance(shard); //which server instance
  verboseStream << "DB client rank " << m_rank << " assigned shard " << shard << " on instance "<< instance << std::endl;
  std::string addr = setup.getInstanceAddress(instance, addr_file_dir);
  std::string db_name = setup.getShardDBname(shard);
  int provider = setup.getShardProviderIndex(shard);
  
  connect(addr, db_name, provider);
}

yk_id_t ADProvenanceDBclient::sendData(const nlohmann::json &entry, const ProvenanceDataType type) const{
  if(!entry.is_object()) throw std::runtime_error("JSON entry must be an object");
  if(m_is_connected){
    std::string entry_s = entry.dump();
    std::cout << "Sending data of size " << entry_s.size() << std::endl;
    return getCollection(type).store(entry_s.data(),entry_s.size());
  }
  return -1;
}

std::vector<yk_id_t> ADProvenanceDBclient::sendMultipleData(const std::vector<std::string> &entries, const ProvenanceDataType type) const{
  if(entries.size() == 0 || !m_is_connected) 
    return std::vector<uint64_t>();

  PerfTimer timer;

  size_t size = entries.size();
  std::vector<uint64_t> ids(size);
  const void* ptrs[size];
  size_t sizes[size];

  for(int i=0;i<size;i++){
    ptrs[i] = (const void*)entries[i].data();
    sizes[i] = entries[i].size();
  }
  if(m_stats){
    m_stats->add("provdb_client_sendmulti_nrecords", size);
    for(int i=0;i<size;i++) m_stats->add("provdb_client_sendmulti_record_size", sizes[i]);
  }

  timer.start();
  getCollection(type).storeMulti(size, ptrs, sizes, ids.data());
  if(m_stats) m_stats->add("provdb_client_sendmulti_send_ms", timer.elapsed_ms());

  return ids;
}






std::vector<yk_id_t> ADProvenanceDBclient::sendMultipleData(const std::vector<nlohmann::json> &entries, const ProvenanceDataType type) const{
  if(entries.size() == 0 || !m_is_connected) 
    return std::vector<uint64_t>();

  PerfTimer timer;

  size_t size = entries.size();
  std::vector<std::string> dump(size);

  for(int i=0;i<size;i++){
    if(!entries[i].is_object()) throw std::runtime_error("Array entries must be JSON objects");
    dump[i] = entries[i].dump();    
  }
  if(m_stats){
    m_stats->add("provdb_client_sendmulti_dump_json_ms", timer.elapsed_ms());
  }
  return sendMultipleData(dump, type);
}

std::vector<yk_id_t> ADProvenanceDBclient::sendMultipleData(const nlohmann::json &entries, const ProvenanceDataType type) const{
  if(!entries.is_array()) throw std::runtime_error("JSON object must be an array");
  size_t size = entries.size();
  
  if(size == 0 || !m_is_connected) 
    return std::vector<uint64_t>();

  std::vector<std::string> dump(size);
  for(int i=0;i<size;i++){
    if(!entries[i].is_object()) throw std::runtime_error("Array entries must be JSON objects");
    dump[i] = entries[i].dump();
  }
  return sendMultipleData(dump, type);
}  
  

void ADProvenanceDBclient::sendDataAsync(const nlohmann::json &entry, const ProvenanceDataType type, OutstandingRequest *req) const{
#ifndef PROVDB_ENABLE_ASYNC
  yk_id_t id = sendData(entry,type);
  if(req != nullptr){
    req->ids.resize(1);
    req->ids[0] = id;
  }
#else  
  if(!entry.is_object()) throw std::runtime_error("JSON entry must be an object");
  if(!m_is_connected) return;

  yk_id_t* ids;
  thallium::eventual<void> *sreq;

  if(req == nullptr){
    ids = nullptr; //utilize sonata mechanism for anomalous request
    sreq = &anom_send_man.getNewRequest();
  }else{
    req->ids.resize(1);
    ids = req->ids.data();
    sreq = &req->req;
  }

  yokan::Collection const* coll = &getCollection(type);
  std::string record = entry.dump();

  thallium::xstream::self().make_thread([ids, sreq, coll, record]() {
      yk_id_t id = coll->store(record.data(), record.size());
      if(ids != nullptr) *ids = id;
      sreq->set_value();
    }, thallium::anonymous());
#endif
}

void ADProvenanceDBclient::sendMultipleDataAsync(const std::vector<std::string> &entries, const ProvenanceDataType type, OutstandingRequest *req) const{
#ifndef PROVDB_ENABLE_ASYNC
  std::vector<yk_id_t> ids = sendMultipleData(entries, type);
  if(req != nullptr) req->ids = std::move(ids);
#else
  if(entries.size() == 0 || !m_is_connected) return;

  PerfTimer timer, ftimer;
  size_t size = entries.size();
  yk_id_t* ids;
  thallium::eventual<void> *sreq;

  if(req == nullptr){
    ids = nullptr; //utilize sonata mechanism for anomalous request
    ftimer.start();
    sreq = &anom_send_man.getNewRequest();
    if(m_stats) m_stats->add("provdb_client_sendmulti_async_getnewreq_ms", ftimer.elapsed_ms());
  }else{
    req->ids.resize(size);
    ids = req->ids.data();
    sreq = &req->req;
  }

  yokan::Collection const* coll = &getCollection(type);

  thallium::xstream::self().make_thread([ids, sreq, coll, entries, size]() {
      yk_id_t tmp[size];
      const void* ptrs[size];
      size_t sizes[size];
      
      for(int i=0;i<size;i++){
	ptrs[i] = (const void*)entries[i].data();
	sizes[i] = entries[i].size();
      }

      coll->storeMulti(size, ptrs, sizes, ids == nullptr ? tmp : ids);
      sreq->set_value();
    }, thallium::anonymous());

  if(m_stats) m_stats->add("provdb_client_sendmulti_async_send_ms", timer.elapsed_ms());
#endif
}



void ADProvenanceDBclient::sendMultipleDataAsync(const std::vector<nlohmann::json> &entries, const ProvenanceDataType type, OutstandingRequest *req) const{
#ifndef PROVDB_ENABLE_ASYNC
  std::vector<yk_id_t> ids = sendMultipleData(entries, type);
  if(req != nullptr) req->ids = std::move(ids);
#else

  if(entries.size() == 0 || !m_is_connected) return;

  PerfTimer timer;
  size_t size = entries.size();
  std::vector<std::string> dump(size);
  for(int i=0;i<size;i++){
    if(!entries[i].is_object()) throw std::runtime_error("Array entries must be JSON objects");
    dump[i] = entries[i].dump();    
  }

  if(m_stats){
    m_stats->add("provdb_client_sendmulti_async_dump_json_ms", timer.elapsed_ms());
    m_stats->add("provdb_client_sendmulti_async_nrecords", size);
    for(int i=0;i<size;i++) m_stats->add("provdb_client_sendmulti_async_record_size", dump[i].size());
  }

  sendMultipleDataAsync(dump, type, req);
#endif
}

void ADProvenanceDBclient::sendMultipleDataAsync(const nlohmann::json &entries, const ProvenanceDataType type, OutstandingRequest *req) const{
#ifndef PROVDB_ENABLE_ASYNC
  std::vector<yk_id_t> ids = sendMultipleData(entries, type);
  if(req != nullptr) req->ids = std::move(ids);
#else

  if(!entries.is_array()) throw std::runtime_error("JSON object must be an array");
  size_t size = entries.size();
  
  if(size == 0 || !m_is_connected) 
    return;

  PerfTimer timer;
  std::vector<std::string> dump(size);
  for(int i=0;i<size;i++){
    if(!entries[i].is_object()) throw std::runtime_error("Array entries must be JSON objects");
    dump[i] = entries[i].dump();
  }

  if(m_stats){
    m_stats->add("provdb_client_sendmulti_async_dump_json_ms", timer.elapsed_ms());
    m_stats->add("provdb_client_sendmulti_async_nrecords", size);
    for(int i=0;i<size;i++) m_stats->add("provdb_client_sendmulti_async_record_size", dump[i].size());
  }

  sendMultipleDataAsync(dump, type, req);
#endif
}  






bool ADProvenanceDBclient::retrieveData(nlohmann::json &entry, yk_id_t index, const ProvenanceDataType type) const{
  if(m_is_connected){
    std::string data;
    try{
      auto const& coll = getCollection(type);
      size_t len = coll.length(index);
      data.resize(len);
      size_t lsize = len;
      coll.load(index, data.data(), &lsize);
      if(lsize != len) fatal_error("Size mismatch");
    }catch(const yokan::Exception &e){
      return false;
    }
    entry = nlohmann::json::parse(data);
    return true;
  }
  return false;
}

void ADProvenanceDBclient::waitForSendComplete(){
  if(m_is_connected)
    anom_send_man.waitAll();
}


void ADProvenanceDBclient::clearAllData(const ProvenanceDataType type) const{
  if(!m_is_connected) return;
  auto &coll = getCollection(type);
  size_t size = coll.size();
  if(size == 0) return;

  std::vector<yk_id_t> doc_ids(size);
  std::vector<void*> fake_docs(size, nullptr);
  std::vector<size_t> fake_sizes(size,0);

  coll.list(0, nullptr, 0, size, doc_ids.data(), fake_docs.data(), fake_sizes.data(), YOKAN_MODE_INCLUSIVE | YOKAN_MODE_IGNORE_DOCS);
  
  coll.eraseMulti(size, doc_ids.data());
}


std::vector<std::string> ADProvenanceDBclient::retrieveAllData(const ProvenanceDataType type) const{
  if(!m_is_connected) return std::vector<std::string>();
  auto &coll = getCollection(type);
  size_t size = coll.size();
  if(size == 0) return std::vector<std::string>();
  
  std::vector<yk_id_t> doc_ids(size);
  std::vector<void*> fake_docs(size, nullptr);
  std::vector<size_t> fake_sizes(size,0);

  coll.list(0, nullptr, 0, size, doc_ids.data(), fake_docs.data(), fake_sizes.data(), YOKAN_MODE_INCLUSIVE | YOKAN_MODE_IGNORE_DOCS);
  
  std::vector<size_t> doc_sizes(size);
  coll.lengthMulti(size, doc_ids.data(), doc_sizes.data());

  std::vector<std::string> docs(size);
  std::vector<void*> doc_ptrs(size);
  for(size_t i=0;i<size;i++){
    docs[i].resize(doc_sizes[i]);
    doc_ptrs[i] = docs[i].data();
  }

  coll.loadMulti(size, doc_ids.data(), doc_ptrs.data(), doc_sizes.data());
  return docs;
}

std::vector<std::string> ADProvenanceDBclient::filterData(const ProvenanceDataType type, const std::string &query) const{
  if(!m_is_connected) return std::vector<std::string>();
  auto &coll = getCollection(type);
  size_t max_size = coll.size();
  if(max_size == 0) return std::vector<std::string>();

  std::vector<yk_id_t> doc_ids(max_size);
  std::vector<void*> fake_docs(max_size, nullptr);
  std::vector<size_t> fake_sizes(max_size,0);

  verboseStream << query << std::endl;

  coll.list(0, query.c_str(), query.size(), max_size, doc_ids.data(), fake_docs.data(), fake_sizes.data(), YOKAN_MODE_INCLUSIVE | YOKAN_MODE_IGNORE_DOCS | YOKAN_MODE_LUA_FILTER);

  verboseStream << "max_size " << max_size << std::endl;
  
  size_t size = max_size;
  for(auto rit = doc_ids.rbegin(); rit != doc_ids.rend(); rit++){
    if(*rit == YOKAN_NO_MORE_DOCS) --size;
    else break;
  }

  verboseStream << "size " << size << std::endl;

  if(size == 0) return std::vector<std::string>();

  doc_ids.resize(size);

  std::vector<size_t> doc_sizes(size);
  coll.lengthMulti(size, doc_ids.data(), doc_sizes.data());

  std::vector<std::string> docs(size);
  std::vector<void*> doc_ptrs(size);
  for(size_t i=0;i<size;i++){
    docs[i].resize(doc_sizes[i]);
    doc_ptrs[i] = docs[i].data();
  }

  coll.loadMulti(size, doc_ids.data(), doc_ptrs.data(), doc_sizes.data());
  return docs;
}



std::unordered_map<std::string,std::string> ADProvenanceDBclient::execute(const std::string &code, const std::unordered_set<std::string>& vars) const{
  assert(0);
  // std::unordered_map<std::string,std::string> out;
  // if(m_is_connected)
  //   m_database.execute(code, vars, &out);
  // return out;
}

void ADProvenanceDBclient::stopServer() const{
  verboseStream << "ADProvenanceDBclient stopping server" << std::endl;
  m_stop_server->on(m_server)();
}

#ifdef ENABLE_MARGO_STATE_DUMP
void ADProvenanceDBclient::serverDumpState() const{
  verboseStream << "ADProvenanceDBclient requesting server dump its state" << std::endl;
  m_margo_dump->on(m_server)();
}
#endif
    
#endif
  

