#include <stdio.h>
#include <math.h>

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>

typedef struct _CalPipeStruct {
    GstElement *pipeline;
    GstElement *alsasrc, *decodebin, *audioconvert, *audioresample, *level, *fakesink;
} CalPipeStruct;

CalPipeStruct pipe;
GMainLoop *loop;

gfloat SMRdBList[15];
gint counter = 0;

static void newElements();
static gboolean setLinks();
static void setProperties();
static void decoder_pad_handler(GstElement *, GstPad *, gpointer);
static void level_handling(GstMessage *);
static void message_cb(GstBus *, GstMessage *, gpointer);



static void
newElements() {
    pipe.pipeline = gst_pipeline_new("pipeline");
    pipe.alsasrc = gst_element_factory_make("alsasrc", "alsasource");
    pipe.decodebin = gst_element_factory_make("decodebin", "decoder");
    pipe.audioresample = gst_element_factory_make ("audioresample", "resample");
    pipe.audioconvert = gst_element_factory_make ("audioconvert", "convert");
    pipe.level = gst_element_factory_make ("level", "level");
    pipe.fakesink = gst_element_factory_make ("fakesink", "fakesink");

    g_assert (pipe.pipeline);
    g_assert (pipe.alsasrc);
    g_assert (pipe.decodebin);
    g_assert (pipe.audioresample);
    g_assert (pipe.audioconvert);
    g_assert (pipe.level);
    g_assert (pipe.fakesink);
}

static gboolean
setLinks () {
    if (!gst_element_link(pipe.alsasrc, pipe.decodebin)) {
        g_error("Couldn't link alsasrc with decodebin\n");
        gst_object_unref(pipe.pipeline);
        return FALSE;
    }
    if (!gst_element_link_many(pipe.audioresample, pipe.audioconvert, pipe.level, pipe.fakesink, NULL)) {
        g_error("Couldn't link audioresample, audioconvert, level and fakesink.\n");
        gst_object_unref(pipe.pipeline);
        return FALSE;
    }
    return TRUE;
}

static void
setProperties () {
    g_object_set (G_OBJECT (pipe.alsasrc), "device", "dsnoop_micwm", NULL);
    g_object_set (G_OBJECT (pipe.fakesink), "async", FALSE, NULL);
    g_object_set (G_OBJECT (pipe.fakesink), "dump", FALSE, NULL);
}

static void
decoder_pad_handler (GstElement *src, GstPad *new_pad, gpointer data) {
    GstPad *sink_pad = gst_element_get_static_pad(pipe.audioresample, "sink");
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    if (gst_pad_is_linked(sink_pad))
        g_print ("We are already linked. Ignoring.\n");
    else {
        new_pad_caps = gst_pad_get_current_caps (new_pad);
        new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
        new_pad_type = gst_structure_get_name (new_pad_struct);
        if (!g_str_has_prefix (new_pad_type, "audio/x-raw"))
            g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
        else if (!(gst_pad_link(new_pad, sink_pad) == GstPadLinkReturn(GST_PAD_LINK_OK))) {
            g_print ("Type is '%s' but link failed.\n", new_pad_type);
            gst_object_unref(pipe.pipeline);
        } else
            g_print ("Link succeeded (type '%s').\n", new_pad_type);
    }
    if (new_pad_caps)
        gst_caps_unref(new_pad_caps);
    gst_object_unref(sink_pad);
}

static void
level_handling(GstMessage *message) {
    const GstStructure *msg_struct = gst_message_get_structure(message);
    const gchar *name = gst_structure_get_name(msg_struct);

    if (g_strcmp0(name, "level") == 0) {
        counter++;
        GstClockTime endtime;
        const GValue *array_val;
        const GValue *value;
        gdouble rms_dB, peak_dB, decay_dB;
        gdouble rms;
        GValueArray *rms_arr, *peak_arr, *decay_arr;

        //Parsing values for statistics
        if (!gst_structure_get_clock_time (msg_struct, "endtime", &endtime))
            g_warning ("Could not parse endtime");

        array_val = gst_structure_get_value (msg_struct, "rms");
        rms_arr = (GValueArray *) g_value_get_boxed (array_val);

        array_val = gst_structure_get_value (msg_struct, "peak");
        peak_arr = (GValueArray *) g_value_get_boxed (array_val);

        array_val = gst_structure_get_value (msg_struct, "decay");
        decay_arr = (GValueArray *) g_value_get_boxed (array_val);

        g_print ("\n    endtime: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (endtime));

        value = g_value_array_get_nth (peak_arr, 0);
        peak_dB = g_value_get_double (value);

        value = g_value_array_get_nth (decay_arr, 0);
        decay_dB = g_value_get_double (value);
        g_print ("      RMS: %f dB, peak: %f dB, decay: %f dB\n",
                 rms_dB, peak_dB, decay_dB);
        value = g_value_array_get_nth (rms_arr, 0);
        rms_dB = g_value_get_double (value);
        g_print ("      RMS: %f dB\n", rms_dB);

        //converting from dB to normal gives us a value between 0.0 and 1.0
        rms = pow (10, rms_dB / 20);
        g_print ("      normalized rms value: %f\n", rms);
        SMRdBList[counter-1] = rms_dB;
        //g_print("SMRdBList[%d] = %f\n", counter-1, SMRdBList[counter-1]);
    }
}

static void
message_cb(GstBus *bus, GstMessage *message, gpointer data) {
    message = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         (GstMessageType)(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_ELEMENT));
    if (message != NULL) {
        switch (GST_MESSAGE_TYPE(message)){
        case GST_MESSAGE_ERROR:
            GError *err;
            gchar *debug_info;
            gst_message_parse_error (message, &err, &debug_info);
            g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (message->src), err->message);
            g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error (&err);
            g_free (debug_info);
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_EOS:
            g_print ("End-Of-Stream reached.\n");
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_STATE_CHANGED:
            // We are only interested in state-changed messages from the pipeline
            if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipe.pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed (message, &old_state, &new_state, &pending_state);
                g_print ("Pipeline state changed from %s to %s:\n",
                         gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
            }
            break;
        case GST_MESSAGE_ELEMENT:
            level_handling(message);
            if (counter == 15)
                gst_element_send_event(pipe.pipeline, gst_event_new_eos());
            break;
        default:
            g_error("Unexpected message received.\n");
            break;
        }
        gst_message_unref(message);
    }
}



































