#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>

//#include <grpcpp/create_channel.h>

//#include "batch.grpc.pb.h"

//#include<unistd.h>

enum State : uint8_t {
    H = 0,
    V = 1,
    //----
    D = 2,
    A = 3,
};

const uint64_t correlation_window_half_width_ps = 350;

struct correlation_time_t {
    int64_t time_lag;
    size_t n_correlations;
};

struct detections_t {
    std::vector<State> measurements;
    std::vector<uint64_t> time_tags;
};

size_t check_correlations(
        const std::vector<uint64_t>& alice_time_tags,
        const std::vector<uint64_t>& bob_time_tags,
        const uint64_t correlation_window_half_width_ps,
        const int64_t delta_time
    ) {

    size_t n_correlations = 0;

    size_t alice_idx = 0;
    size_t bob_idx = 0;
    size_t alice_size = alice_time_tags.size();
    size_t bob_size = bob_time_tags.size();
    
    if (delta_time >= 0) {

        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] > bob_time_tags[bob_idx] + delta_time + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps <= bob_time_tags[bob_idx] + delta_time
                    && bob_time_tags[bob_idx] + delta_time <= alice_time_tags[alice_idx] + correlation_window_half_width_ps) {
                    ++n_correlations;
                    break;
                }
            }
        }

    } else {

        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] - delta_time > bob_time_tags[bob_idx] + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps - delta_time <= bob_time_tags[bob_idx]
                    && bob_time_tags[bob_idx] <= alice_time_tags[alice_idx] + correlation_window_half_width_ps - delta_time) {
                    ++n_correlations;
                    break;
                }
            }
        }
    }

    return n_correlations;
}

correlation_time_t find_time_lag(
        const std::vector<uint64_t>& alice_time_tags, 
        const std::vector<uint64_t>& bob_time_tags, 
        uint64_t correlation_window_half_width_ps
    ) {

    int64_t step = 1;
    
    int64_t first_delta_time = -1000;
    int64_t maximum_delta = 1000;

    //std::cout << "The number of iterations is: " << (maximum_delta - first_delta_time) / step << ". Beginning with: " << first_delta_time << ", ending with " << maximum_delta << "\n";

    correlation_time_t max_correlation {0, 0};

    //auto init_time = std::chrono::high_resolution_clock::now();

    #pragma omp declare reduction(maximo : struct correlation_time_t : omp_out = omp_in.n_correlations > omp_out.n_correlations ? omp_in : omp_out)
    #pragma omp parallel for reduction(maximo: max_correlation)
    for (int64_t delta_time = first_delta_time; delta_time < maximum_delta; delta_time += step) {
        size_t correlations = check_correlations(alice_time_tags, bob_time_tags, correlation_window_half_width_ps, delta_time);
        if (correlations > max_correlation.n_correlations) {
            max_correlation.time_lag = delta_time;
            max_correlation.n_correlations = correlations;
        }
    }

    //auto end_time = std::chrono::high_resolution_clock::now();

    //uint64_t total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - init_time).count();
    //std::cout << "Time = " << ((double) total_time) / 1e9  << " s\n";

    max_correlation.n_correlations = check_correlations(alice_time_tags, bob_time_tags, correlation_window_half_width_ps, max_correlation.time_lag);
    //std::cout << "\nFinal number of correlations obtained = " << max_correlation.n_correlations << " for delta_time = " << max_correlation.time_lag << " with step = " << step << "\n\n";
    
    return max_correlation;
}

std::vector<std::pair<size_t, size_t>> find_corrrelated_indices (
        const std::vector<uint64_t>& alice_time_tags, 
        const std::vector<uint64_t>& bob_time_tags, 
        const uint64_t correlation_window_half_width_ps,
        const int64_t delta_time
    ) {

    std::vector<std::pair<size_t, size_t>> indices;

    size_t alice_idx = 0;
    size_t bob_idx = 0;
    size_t alice_size = alice_time_tags.size();
    size_t bob_size = bob_time_tags.size();
    
    if (delta_time >= 0) {
        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] > bob_time_tags[bob_idx] + delta_time + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps <= bob_time_tags[bob_idx] + delta_time
                    && bob_time_tags[bob_idx] + delta_time <= alice_time_tags[alice_idx] + correlation_window_half_width_ps) {
                    indices.push_back(std::pair<size_t, size_t>(alice_idx, bob_idx));
                    ++alice_idx;
                    break;
                }
            }
        }

    } else {
        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] - delta_time > bob_time_tags[bob_idx] + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps - delta_time <= bob_time_tags[bob_idx]
                    && bob_time_tags[bob_idx] <= alice_time_tags[alice_idx] + correlation_window_half_width_ps - delta_time) {
                    indices.push_back(std::pair<size_t, size_t>(alice_idx, bob_idx));
                    ++alice_idx;
                    break;
                }
            }
        }
    }
    return indices;
}

/*void write_data(const batch::Batches& batches, const std::string& file_name) {
    std::ofstream server_batch(file_name + "_alice.detections");
    
    for (const common::DisclosedTag& tag : batches.tags_server().tags())
        server_batch << "(" << tag.time() << "," << tag.base() << ")" << ",";
    server_batch.close();

    server_batch.open(file_name + "_alice.sifted_key");
    for (uint8_t byte : batches.sifted_key_server().bytes())
        server_batch << byte << ',';
    server_batch.close();

    std::ofstream client_batch(file_name + "_bob.detections");
    for (const common::DisclosedTag& tag : batches.tags_client().tags())
        client_batch << "(" << tag.time() << "," << tag.base() << ")" << ",";
    client_batch.close();

    client_batch.open(file_name + "_bob.sifted_key");
    for (uint8_t byte : batches.sifted_key_client().bytes())
        client_batch << byte << ',';
    server_batch.close();

    std::cout << file_name << " created!\n";

    return;    
}*/

void read_detections(
        const std::string& file_name,
        detections_t& alice_detections,
        detections_t& bob_detections
    ) {
    
    alice_detections.time_tags.clear();
    bob_detections.time_tags.clear();
    alice_detections.measurements.clear();
    bob_detections.measurements.clear();


    std::ifstream alice_stream, bob_stream;
    if (!std::filesystem::exists(file_name + "_alice.detections")) {
        std::cout << "Can't find file: " << file_name << "_alice.detections\n";
        return;
    }
    if (!std::filesystem::exists(file_name + "_bob.detections")) {
        std::cout << "Can't find file: " << file_name << "_bob.detections\n";
        return;
    }
    alice_stream.open(file_name + "_alice.detections", std::ios_base::in);

    char next_char = 0;
    std::string int_str;

    while(alice_stream) {
        next_char = alice_stream.get();
        if (next_char == '(') {
            while (next_char != ',' && alice_stream) {
                next_char = alice_stream.get();
                int_str += next_char;
            }
            alice_detections.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
            int_str.clear();

            while (next_char != ')' && alice_stream) {
                next_char = alice_stream.get();
                int_str += next_char;
            }
            int state = strtoul(int_str.c_str(), NULL, 10);
            if (state < 0 && state > 3)
                throw "State is not correctly formated";
            alice_detections.measurements.push_back(State(state));
            
            int_str.clear();
        }
    }
    alice_stream.close();

    try {
        bob_stream.open(file_name + "_bob.detections");
    } catch (const std::ifstream& excep) {
        std::cout << "Can't open file: " << file_name << "_alice.detections\n";
        throw;
    }

    while(bob_stream) {
        next_char = bob_stream.get();
        if (next_char == '(') {
            while (next_char != ',' && bob_stream) {
                next_char = bob_stream.get();
                int_str += next_char;
            }
            bob_detections.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
            int_str.clear();

            while (next_char != ')' && bob_stream) {
                next_char = bob_stream.get();
                int_str += next_char;
            }
           int state = strtoul(int_str.c_str(), NULL, 10);
            if (state < 0 && state > 3)
                throw "State is not correctly formated";
            bob_detections.measurements.push_back(State(state));
            int_str.clear();
        }
    }
    bob_stream.close();
}

int main(int argc, char* argv[]) {

    /*std::shared_ptr<grpc::Channel> channel(grpc::CreateChannel("192.168.0.231:40001", grpc::InsecureChannelCredentials()));
    std::unique_ptr<batch::BatchService::Stub> stub = batch::BatchService::NewStub(channel);

    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientReader<batch::Batches>> reader(stub->Subscribe(&context, common::Empty()));

    batch::Batches batches;
    size_t counter = 0;
    while (reader->Read(&batches)) {
        std::vector<uint64_t> alice_times_ps, bob_times_ps;
        std::vector<State> alice_states, bob_states;

        alice_times_ps.reserve(batches.tags_server().tags_size());
        //alice_states.reserve(batches.tags_server().tags_size());

        for (const common::DisclosedTag& tag : batches.tags_server().tags())
            alice_times_ps.push_back(tag.time());
        
        bob_times_ps.reserve(batches.tags_client().tags_size());
        //bob_states.reserve(batches.tags_client().tags_size());

        for (common::DisclosedTag tag : batches.tags_client().tags())
            bob_times_ps.push_back(tag.time());

        correlation_time_t max_correlation = find_time_lag(alice_times_ps, bob_times_ps, correlation_window_half_width_ps);
        size_t correlations = check_correlations(alice_times_ps, bob_times_ps, correlation_window_half_width_ps, max_correlation.time_lag);

        std::cout << "Total correlations = " << correlations << "\n";

        //write_data(batches, std::string("batch") + std::to_string(counter));
        //++counter;static_cast<double>(
    }

    grpc::Status status = reader->Finish();
    if (status.ok()) {
      std::cout << "Raw key rpc succeeded." << std::endl;
    } else {
      std::cout << "Raw key rpc failed." << std::endl;
    }*/
    
    std::string batch_str(
        argc > 1? argv[1] : "../batches/batch1"
    );

    detections_t alice_detections, bob_detections;
    read_detections(batch_str, alice_detections, bob_detections);

    /**
     * In the complete scenario (where Alice only has access to her measurements and Bob to his owns), Bob will sed alices only his time tags.
     * Then Alice will perform the next three steps to find the indinices in which they think they have a correlated detection
     */

    correlation_time_t max_correlation = find_time_lag(alice_detections.time_tags, bob_detections.time_tags, correlation_window_half_width_ps);
    size_t correlations = check_correlations(alice_detections.time_tags, bob_detections.time_tags, correlation_window_half_width_ps, max_correlation.time_lag);
    std::vector<std::pair<size_t, size_t>> corrrelated_indices = find_corrrelated_indices(alice_detections.time_tags, bob_detections.time_tags, correlation_window_half_width_ps, max_correlation.time_lag);

    //Alice will send Bob the right indices

    //Now Alice will prepare the "true" vector of measurements (the one containing only "valid" measurements)
    std::vector<State> true_coincidences;
    true_coincidences.reserve(corrrelated_indices.size());
    for (const std::pair<size_t, size_t>& index_pair : corrrelated_indices)
        true_coincidences.push_back(alice_detections.measurements[index_pair.first]);

    //This part is simulation, in practice, Bob will send only part of his measurements to chech the QBER. And then they will apply error correction and privacy amplificacion.
    //Here we are only going to calculate the QBER using all the measurements

    std::vector<State> bob_true_coincidences;
    bob_true_coincidences.reserve(corrrelated_indices.size());
    for (const std::pair<size_t, size_t>& index_pair : corrrelated_indices)
        bob_true_coincidences.push_back(bob_detections.measurements[index_pair.second]);

    //Checking that the size is correct

    /**
     * @var right_measurements: = measurments in which Alice and Bob has measured wrt the same basis and obtained the same result. (this are the measurements that are used to extrac the secret key)
     * @var wrong_measurements: = measurments in which Alice and Bob has measured wrt the same basis and obtained a different result. QBER = wrong / (right + wrong)
     * @var invalid_measurements: = measurments in which Alice and Bob has measured wrt different base (this measurements doesn't provide any information)
     */

    size_t right_measurements = 0,
           wrong_measurements = 0,
           invalid_measurements = 0;

    for (size_t index = 0; index < corrrelated_indices.size(); ++index){
        if (true_coincidences[index] == bob_true_coincidences[index])
            ++right_measurements;
        else if ((true_coincidences[index] < 2 && bob_true_coincidences[index] < 2) || (true_coincidences[index] > 1 && bob_true_coincidences[index] > 1))
            ++wrong_measurements;
        else
            ++invalid_measurements;
    }

    std::cout << right_measurements << " " << wrong_measurements << " " << invalid_measurements << "\n"
              << (right_measurements + wrong_measurements + invalid_measurements) << " = " << correlations << "\n"
              << "QBER = " << static_cast<double>(wrong_measurements) / static_cast<double>(right_measurements + wrong_measurements) << "\n\n";
    
    return 0;
}