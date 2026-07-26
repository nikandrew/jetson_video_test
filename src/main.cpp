#include <gst/gst.h>
#include <glib-unix.h>

#include <cerrno>
#include <csignal>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct Options {
    int sensor_id = 0;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int flip_method = 0;
};

struct RuntimeState {
    GMainLoop* loop = nullptr;
    bool had_error = false;
};

void print_usage(const char* program) {
    std::cout
        << "Jetson CSI camera viewer\n\n"
        << "Usage: " << program << " [options]\n\n"
        << "Options:\n"
        << "  --sensor-id N     Argus sensor number (default: 0)\n"
        << "  --width N         Frame width (default: 1920)\n"
        << "  --height N        Frame height (default: 1080)\n"
        << "  --fps N           Frame rate (default: 30)\n"
        << "  --flip-method N   nvvidconv flip method, 0..7 (default: 0)\n"
        << "  -h, --help        Show this help\n\n"
        << "Press Ctrl+C to stop.\n";
}

bool parse_positive_int(const char* text, int* value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        return false;
    }

    *value = static_cast<int>(parsed);
    return true;
}

bool parse_nonnegative_int(const char* text, int* value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < 0 || parsed > INT_MAX) {
        return false;
    }

    *value = static_cast<int>(parsed);
    return true;
}

bool read_value(int argc, char* argv[], int* index, const std::string& name,
                int* destination, bool allow_zero = false) {
    if (*index + 1 >= argc) {
        std::cerr << "Missing value after " << name << "\n";
        return false;
    }

    ++(*index);
    const bool valid = allow_zero
        ? parse_nonnegative_int(argv[*index], destination)
        : parse_positive_int(argv[*index], destination);
    if (!valid) {
        std::cerr << "Invalid value for " << name << ": " << argv[*index] << "\n";
    }
    return valid;
}

bool parse_options(int argc, char* argv[], Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            print_usage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        if (argument == "--sensor-id") {
            if (!read_value(argc, argv, &i, argument, &options->sensor_id, true)) return false;
        } else if (argument == "--width") {
            if (!read_value(argc, argv, &i, argument, &options->width)) return false;
        } else if (argument == "--height") {
            if (!read_value(argc, argv, &i, argument, &options->height)) return false;
        } else if (argument == "--fps") {
            if (!read_value(argc, argv, &i, argument, &options->fps)) return false;
        } else if (argument == "--flip-method") {
            if (!read_value(argc, argv, &i, argument, &options->flip_method, true)) return false;
            if (options->flip_method > 7) {
                std::cerr << "--flip-method must be in range 0..7\n";
                return false;
            }
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

gboolean stop_loop(gpointer user_data) {
    auto* state = static_cast<RuntimeState*>(user_data);
    g_main_loop_quit(state->loop);
    return G_SOURCE_CONTINUE;
}

gboolean on_bus_message(GstBus*, GstMessage* message, gpointer user_data) {
    auto* state = static_cast<RuntimeState*>(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            std::cerr << "GStreamer error: " << (error ? error->message : "unknown") << "\n";
            if (debug != nullptr) {
                std::cerr << "Details: " << debug << "\n";
            }
            g_clear_error(&error);
            g_free(debug);
            state->had_error = true;
            g_main_loop_quit(state->loop);
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(message, &error, &debug);
            std::cerr << "GStreamer warning: " << (error ? error->message : "unknown") << "\n";
            g_clear_error(&error);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS:
            g_main_loop_quit(state->loop);
            break;
        default:
            break;
    }
    return G_SOURCE_CONTINUE;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        return EXIT_FAILURE;
    }

    gst_init(nullptr, nullptr);

    std::ostringstream pipeline_text;
    pipeline_text
        << "nvarguscamerasrc sensor-id=" << options.sensor_id
        << " ! video/x-raw(memory:NVMM),width=(int)" << options.width
        << ",height=(int)" << options.height
        << ",format=(string)NV12,framerate=(fraction)" << options.fps << "/1"
        << " ! nvvidconv flip-method=" << options.flip_method
        << " ! nvegltransform ! nveglglessink sync=false";

    std::cout << "Starting pipeline:\n" << pipeline_text.str() << "\n";

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeline_text.str().c_str(), &error);
    if (pipeline == nullptr || error != nullptr) {
        std::cerr << "Cannot create GStreamer pipeline: "
                  << (error ? error->message : "unknown error") << "\n";
        g_clear_error(&error);
        if (pipeline != nullptr) gst_object_unref(pipeline);
        return EXIT_FAILURE;
    }

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    RuntimeState runtime;
    runtime.loop = loop;
    GstBus* bus = gst_element_get_bus(pipeline);
    const guint bus_watch = gst_bus_add_watch(bus, on_bus_message, &runtime);
    const guint signal_watch = g_unix_signal_add(SIGINT, stop_loop, &runtime);

    const GstStateChangeReturn state_result = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    int exit_code = EXIT_SUCCESS;
    if (state_result == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Cannot start the camera pipeline.\n";
        exit_code = EXIT_FAILURE;
    } else {
        g_main_loop_run(loop);
        if (runtime.had_error) {
            exit_code = EXIT_FAILURE;
        }
    }

    gst_element_send_event(pipeline, gst_event_new_eos());
    gst_element_set_state(pipeline, GST_STATE_NULL);

    g_source_remove(signal_watch);
    g_source_remove(bus_watch);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return exit_code;
}
