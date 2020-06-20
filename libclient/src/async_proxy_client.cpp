//
// Created by Lloyd Brown on 10/1/19.
//

#include "async_proxy_client.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;

void async_proxy_client::init(const std::string &host_name, int port) {
    done_ = new std::atomic<bool>(false);
    total_ = new std::atomic<int>(0);

    auto socket = std::make_shared<TSocket>(host_name, port);
    socket->setRecvTimeout(10000);
    socket->setSendTimeout(120000);
    transport_ = std::shared_ptr<TTransport>(new TFramedTransport(socket));
    protocol_ = std::shared_ptr<TProtocol>(new TBinaryProtocol(transport_));
    client_ = std::make_shared<pancake_thriftConcurrentClient>(protocol_);
    transport_->open();
    requests_ = std::make_shared<queue<int>>();
    client_id_ = get_client_id();
    seq_id_ = sequence_id();
    seq_id_.__set_client_id(client_id_);
    response_thread_ = new std::thread(&async_proxy_client::read_responses, this);
    reader_ = command_response_reader(protocol_);
}

int64_t async_proxy_client::get_client_id() {
  auto id = client_->get_client_id();
  auto block_id_ = 1;
  client_->register_client_id(block_id_, id);
  return id;
}


std::string async_proxy_client::get(const std::string &key) {
    seq_id_.__set_client_seq_no(sequence_num++);
    client_->async_get(seq_id_, key);
    requests_->push(GET);
    return "";
}

void async_proxy_client::put(const std::string &key, const std::string &value) {
    seq_id_.__set_client_seq_no(sequence_num++);
    client_->async_put(seq_id_, key, value);
    requests_->push(PUT);
}

std::vector<std::string> async_proxy_client::get_batch(const std::vector<std::string> &keys) {
    try {
        seq_id_.__set_client_seq_no(sequence_num++);
        client_->async_get_batch(seq_id_, keys);
        requests_->push(GET_BATCH);
    }
    catch(apache::thrift::transport::TTransportException e) {
        (void)0;
    }
    std::vector<std::string> fake_vec;
    return fake_vec;
}

void async_proxy_client::put_batch(const std::vector<std::string> &keys, const std::vector<std::string> &values) {
    seq_id_.__set_client_seq_no(sequence_num++);
    client_->async_put_batch(seq_id_, keys, values);
    requests_->push(PUT_BATCH);
}

void async_proxy_client::read_responses() { 
    while (!done_->load()) {
        auto type = requests_->pop();
        std::vector<std::string> _return;
        try {
            reader_.recv_response(_return);
        } catch(apache::thrift::transport::TTransportException e){
            (void)0;
        }
        *total_ += _return.size();
    }
}

int async_proxy_client::num_requests_satisfied(){
    return total_->load();
}

void async_proxy_client::finish(){
    done_->store(true);
    requests_->push(GET_BATCH);
    requests_->push(GET_BATCH);
    requests_->push(GET_BATCH);
    requests_->push(GET_BATCH);
    requests_->push(GET_BATCH);
    sleep(5);
    response_thread_->join();
}
