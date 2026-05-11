#include <iostream>
#include <memory>
#include <string>
#include <getopt.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "commoncod/proto_mapping.h"
#include "commoncod/utils.h"
#include "server/update_slots.h"   // brings in server-context.h — do NOT include it again
#include "grpccod/service.h"

#include "llama.h"
#include "common.h"
#include "sampling.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

std::atomic<bool> loaded_model{false};
std::mutex model_mutex;
std::condition_variable model_cv;

server_context ctx_server;

static void setup_queue_callbacks(server_context& ctx);

int main(int argc, char** argv) {
    std::string server_address = "localhost:50051";

    struct option long_options[] = {
        {"addr", required_argument, nullptr, 'a'},
        {nullptr, 0, nullptr, 0}
    };

    int option, option_index = 0;
    while ((option = getopt_long(argc, argv, "a:", long_options, &option_index)) != -1) {
        switch (option) {
            case 'a': server_address = optarg; break;
            default:
                std::cerr << "Usage: " << argv[0] << " [--addr=<address>]" << std::endl;
                return 1;
        }
    }

    std::cout << "GRPC Service Started on " << server_address << std::endl;

    llama_backend_init();
    llama_numa_init(gpt_params{}.numa);

    setup_queue_callbacks(ctx_server);

    std::thread queue_thread([]() {   // ctx_server is global — no capture needed
        {
            std::unique_lock<std::mutex> lock(model_mutex);
            model_cv.wait(lock, [] { return loaded_model.load(); });
        }
        ctx_server.queue_tasks.start_loop();
    });

    BackendServiceImpl service(ctx_server);
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetMaxMessageSize(50 * 1024 * 1024);
    builder.SetMaxSendMessageSize(50 * 1024 * 1024);
    builder.SetMaxReceiveMessageSize(50 * 1024 * 1024);

    std::unique_ptr<Server> server = builder.BuildAndStart();
    std::cout << "✅ Server listening on " << server_address << std::endl;

    server->Wait();

    ctx_server.queue_tasks.terminate();
    loaded_model = false;
    model_cv.notify_all();
    queue_thread.join();
    llama_backend_free();

    std::cout << "🛑 Server stopped" << std::endl;
    return 0;
}

static void setup_queue_callbacks(server_context& ctx) {
    ctx.queue_tasks.on_new_task([&ctx](server_task&& task) {
        ctx.process_single_task(std::move(task));   // fix: std::move
    });

    // Fix: direct field assignment, not setter methods
    ctx.queue_tasks.callback_update_slots = [&ctx]() {
        ctx.update_slots();
    };

    ctx.queue_tasks.callback_finish_multitask = [&ctx](server_task_multi& m) {
        ctx.on_finish_multitask(m);
    };
}