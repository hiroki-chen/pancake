//
// Created by Lloyd Brown on 10/5/19.
//


#include <unordered_map>
#include <iostream>

#include "distribution.h"
#include "pancake_proxy.h"
//#include "thrift_response_client_map.h"
#include "thrift_server.h"
#include "thrift_utils.h"

#define HOST "127.0.0.1"
#define PROXY_PORT 9090

typedef std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> trace_vector;

distribution load_frequencies_from_trace(const std::string &trace_location, trace_vector &trace_, int client_batch_size_) {
    std::vector<std::string> get_keys;
    std::vector<std::string> put_keys;
    std::vector<std::string> put_values;

    std::unordered_map<std::string, int> key_to_frequency;
    int frequency_sum = 0;
    std::string op, key, val;
    std::ifstream in_workload_file;
    in_workload_file.open(trace_location, std::ios::in);
    if(!in_workload_file.is_open()){
        std::perror("Unable to find workload file");
    }
    if(in_workload_file.fail()){
        std::perror("Opening workload file failed");
    }
    std::string line;
    while (std::getline(in_workload_file, line)) {
        op = line.substr(0, line.find(" "));
        key = line.substr(line.find(" ")+1);
        val = "";

        if (key.find(" ") != -1) {
            val = key.substr(key.find(" ")+1);
            key = key.substr(0, key.find(" "));
        }
        if(val == ""){
            get_keys.push_back(key);
            if (get_keys.size() == client_batch_size_){
                trace_.push_back(std::make_pair(get_keys, std::vector<std::string>()));
                get_keys.clear();
            }
        }
        else {
            put_keys.push_back(key);
            put_values.push_back(val);
            if (put_keys.size() == client_batch_size_){
                trace_.push_back(std::make_pair(put_keys, put_values));
                put_keys.clear();
                put_values.clear();
            }
        }
        assert (key != "PUT");
        assert (key != "GET");
        if (key_to_frequency.count(key) == 0){
            key_to_frequency[key] = 1;
            frequency_sum += 1;
        }
        else {
            key_to_frequency[key] += 1;
            frequency_sum += 1;
        }
    }
    if (get_keys.size() > 0){
        trace_.push_back(std::make_pair(get_keys, std::vector<std::string>()));
        get_keys.clear();
    }
    if (put_keys.size() > 0){
        trace_.push_back(std::make_pair(put_keys, put_values));
        put_keys.clear();
        put_values.clear();
    }
    std::vector<std::string> keys;
    std::vector<double> frequencies;
    for (auto pair: key_to_frequency){
        keys.push_back(pair.first);
        frequencies.push_back(pair.second/(double)frequency_sum);
    }
    in_workload_file.close();
    distribution dist(keys, frequencies);
    return dist;
};

void flush_thread(std::shared_ptr<proxy> proxy){
    while (true){
        sleep(1);
        dynamic_cast<pancake_proxy&>(*proxy).flush();
    }
    std::cout << "Quitting flush thread" << std::endl;
}

void usage() {
    std::cout << "Pancake proxy: frequency flattening kvs\n";
    // Network Parameters
    std::cout << "\t -h: Storage server host name\n";
    std::cout << "\t -p: Storage server port\n";
    std::cout << "\t -s: Storage server type (redis, rocksdb, memcached)\n";
    std::cout << "\t -n: Storage server count\n";
    std::cout << "\t -z: Proxy server type\n";
    // Workload parameters
    std::cout << "\t -l: Workload file\n";
    std::cout << "\t -v: Value size\n";
    std::cout << "\t -b: Security batch size\n";
    std::cout << "\t -c: Storage batch size\n";
    std::cout << "\t -t: Number of worker threads for cpp_redis\n";
    // Other parameters
    std::cout << "\t -o: Output location for sizing thread\n";
    std::cout << "\t -d: Core to run on\n";
};

int main(int argc, char *argv[]) {
    int client_batch_size = 50;
    std::atomic<int> xput;
    std::atomic_init(&xput, 0);
    int object_size_ = 1000;

    std::shared_ptr<proxy> proxy_ = std::make_shared<pancake_proxy>();
    int o;
    std::string proxy_type_ = "pancake";
    while ((o = getopt(argc, argv, "h:p:s:n:v:b:c:t:o:d:z:q:l:")) != -1) {
        switch (o) {
            case 'h':
                dynamic_cast<pancake_proxy&>(*proxy_).server_host_name_ = std::string(optarg);
                break;
            case 'p':
                dynamic_cast<pancake_proxy&>(*proxy_).server_port_ = std::atoi(optarg);
                break;
            case 's':
                dynamic_cast<pancake_proxy&>(*proxy_).server_type_ = std::string(optarg);
                break;
            case 'n':
                dynamic_cast<pancake_proxy&>(*proxy_).server_count_ = std::atoi(optarg);
                break;
            case 'v':
                dynamic_cast<pancake_proxy&>(*proxy_).object_size_ = std::atoi(optarg);
                break;
            case 'b':
                dynamic_cast<pancake_proxy&>(*proxy_).security_batch_size_ = std::atoi(optarg);
                break;
            case 'c':
                dynamic_cast<pancake_proxy&>(*proxy_).storage_batch_size_ = std::atoi(optarg);
                break;
            case 't':
                dynamic_cast<pancake_proxy&>(*proxy_).p_threads_ = std::atoi(optarg);
                break;
            case 'o':
                dynamic_cast<pancake_proxy&>(*proxy_).output_location_ = std::string(optarg);
                break;
            case 'd':
                dynamic_cast<pancake_proxy&>(*proxy_).core_ = std::atoi(optarg) - 1;
                break;
            case 'z':
                proxy_type_ = std::string(optarg);
                break;
            case 'q':
                client_batch_size = std::atoi(optarg);
                break;
            case 'l':
                dynamic_cast<pancake_proxy&>(*proxy_).trace_location_ = std::string(optarg);
                break;
            default:
                usage();
                exit(-1);
        }
    }

    void *arguments[4];
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> trace_;
    assert(dynamic_cast<pancake_proxy&>(*proxy_).trace_location_ != "");
    auto dist = load_frequencies_from_trace(dynamic_cast<pancake_proxy&>(*proxy_).trace_location_, trace_, client_batch_size);

    arguments[0] = &dist;
    auto items = dist.get_items();
    double alpha = 1.0 / items.size();
    double delta = 1.0 / (2 * items.size()) * 1 / alpha;
    auto id_to_client = std::make_shared<thrift_response_client_map>();
    arguments[1] = &alpha;
    arguments[2] = &delta;
    arguments[3] = &id_to_client;
    std::string dummy(object_size_, '0');
    std::cout <<"Initializing pancake" << std::endl;
    dynamic_cast<pancake_proxy&>(*proxy_).init(items, std::vector<std::string>(items.size(), dummy), arguments);
    std::cout << "Initialized pancake" << std::endl;
    auto proxy_server = thrift_server::create(proxy_, "pancake", id_to_client, PROXY_PORT, 1);
    std::thread proxy_serve_thread([&proxy_server] { proxy_server->serve(); });
    wait_for_server_start(HOST, PROXY_PORT);
    std::cout << "Proxy server is reachable" << std::endl;
    sleep(10000);
    //flush_thread(proxy_);
    //proxy_->close();
    //proxy_server->stop();
}
