#include "AmbNoiseCal.h"

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    GstBus *bus;

    newElements();
    gst_bin_add_many(GST_BIN(pipe.pipeline), pipe.alsasrc, pipe.decodebin, pipe.audioresample,
                     pipe.audioconvert, pipe.level, pipe.fakesink, NULL);
    if (!setLinks()) return -1;
    setProperties();
    g_signal_connect(pipe.decodebin, "pad-added", G_CALLBACK(decoder_pad_handler), NULL);

    loop = g_main_loop_new(NULL, FALSE);
    bus = gst_element_get_bus(pipe.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message", G_CALLBACK(message_cb), NULL);

    GstStateChangeReturn returnstate = gst_element_set_state(pipe.pipeline, GST_STATE_PLAYING);

    if (returnstate == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set pipeline to PLAYING. Executing.\n");
        gst_object_unref (pipe.pipeline);
        return -1;
    }
    g_main_loop_run(loop);

    //Free resources
    gst_bus_remove_signal_watch(bus);
    gst_object_unref(bus);
    g_main_loop_unref (loop);
    gst_element_set_state (pipe.pipeline, GST_STATE_NULL);
    gst_object_unref (pipe.pipeline);

    gfloat accumulator = 0;
    for (int i = 0; i < 15; i++) {
        //g_print("SMRdBList[%d] = %f\n", i, SMRdBList[i]);
        accumulator += SMRdBList[i];
    }

    FILE *calFile = fopen("/home/root/WordRemove/ambiencedB", "w");
    fprintf(calFile, "%f", (accumulator/(counter))+6);
    fclose(calFile);
}
