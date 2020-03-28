#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include <iostream>
#include <gstreamer-1.0/gst/gststructure.h>
#include <gstreamer-1.0/gst/gstcaps.h>
#include "opencv2/opencv.hpp"
using namespace cv;
using namespace std;
std::deque<Mat> frameQueue;

//`pkg-config --libs gstreamer-1.0` `pkg-config --libs opencv` `pkg-config --libs gstreamer-app-1.0`

GstFlowReturn
new_preroll(GstAppSink *appsink, gpointer data) {
    g_print("Got preroll!\n");
    return GST_FLOW_OK;
}

GstFlowReturn new_sample(GstAppSink *appsink, gpointer data) {
    static int framecount = 0;
    framecount++;
    gint width, height;
    GstMapInfo map;
    const GstStructure *str;
    const GstStructure *info;

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    GST_BUFFER_DTS(buffer);

    str = gst_caps_get_structure(caps, 0);
    if (!gst_structure_get_int(str, "width", &width) || !gst_structure_get_int(str, "height", &height)) {
        g_print("No width/height available\n");
    }
    // show caps on first frame
    if (framecount == 1) {
        g_print("%s\n", gst_caps_to_string(caps));
        g_print("The video size of this set of capabilities is %dx%d\n", width, height);
    }

    info = gst_sample_get_info(sample);

    if (info != NULL)
        cout << gst_structure_to_string(info) << endl;

    gst_buffer_map(buffer, &map, GST_MAP_READ);

    // convert gstreamer data to OpenCV Mat, you could actually
    // resolve height / width from caps...
    Mat frame(Size(width, height), CV_8UC3, (char*) map.data);
    // TODO: synchronize this....
    frameQueue.push_back(frame);
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    //    g_print("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR:
        {
            GError *err;
            gchar *debug;

            gst_message_parse_error(message, &err, &debug);
            g_print("Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS:
            //     end-of-stream 
            break;
        default:
            //     unhandled message 
            break;
    }
    /* we want to be notified again the next time there is a message
     * on the bus, so returning TRUE (FALSE means we want to stop watching
     * for messages on the bus and our callback should not be called again)
     */
    return TRUE;
}

int
main(int argc, char *argv[]) {

    GError *error = NULL;
    gst_init(&argc, &argv);

    gchar *descr = g_strdup(
            "v4l2src device=/dev/video1 !"
            "video/x-raw ! "
            "videoconvert ! "
            "video/x-raw,format=BGR, width=1280, height=720 ! "
            "videoconvert ! "
            "appsink name=sink sync=true"
            );
    GstElement *pipeline = gst_parse_launch(descr, &error);

    if (error != NULL) {
        g_print("could not construct pipeline: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

    gst_app_sink_set_emit_signals((GstAppSink*) sink, TRUE);
    gst_app_sink_set_drop((GstAppSink*) sink, true);
    gst_app_sink_set_max_buffers((GstAppSink*) sink, 1);
    GstAppSinkCallbacks callbacks = {NULL, new_preroll, new_sample};
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, NULL, NULL);

    GstBus *bus;
    guint bus_watch_id;
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);
    gst_object_unref(bus);

    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

    while (true) {
        g_main_iteration(false);
        // TODO: synchronize...
        if (frameQueue.size() > 0) {
            // this lags pretty badly even when grabbing frames from webcam
            Mat frame = frameQueue.front();
            imshow("edges", frame);
            const char key = (char) cv::waitKey(2);
            if (key == 27 || key == 'q') {
                cout << "Exit requested" << endl;
                break;
            }
            frameQueue.clear();
        }
    }
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));

    return 0;
}