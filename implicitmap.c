//
// implicitmap.c
// a maxmsp and puredata external encapsulating the functionality of libmapper
// for performing implicit mapping 
// http://www.idmil.org/software/libmapper
// Joseph Malloch, IDMIL 2010
//
// This software was written in the Input Devices and Music Interaction
// Laboratory at McGill University in Montreal, and is copyright those
// found in the AUTHORS file.  It is licensed under the GNU Lesser Public
// General License version 2.1 or later.  Please see COPYING for details.
// 

// *********************************************************
// -(Includes)----------------------------------------------

#ifdef MAXMSP
    #include "ext.h"            // standard Max include, always required
    #include "ext_obex.h"       // required for new style Max object
    #include "ext_dictionary.h"
    #include "jpatcher_api.h"
#else
    #include "m_pd.h"
    #define A_SYM A_SYMBOL
#endif
#include "mapper/mapper.h"
#include "mapper/mapper_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <arpa/inet.h>

#define INTERVAL 1
#define MAX_LIST 256

// *********************************************************
// -(object struct)-----------------------------------------
typedef struct _signal_ref
{
    void *x;
    int offset;
} t_signal_ref;

typedef struct _snapshot
{
    int id;
    float *inputs;
    float *outputs;
    struct _snapshot *next;
} *t_snapshot;

typedef struct _implicitmap
{
    t_object ob;
    void *outlet1;
    void *outlet2;
    void *outlet3;
    void *clock;          // pointer to clock object
    void *timeout;
    char *name;
    mapper_admin admin;
    mapper_device device;
    mapper_monitor monitor;
    mapper_db db;
    int ready;
    int mute;
    int new_in;
    int num_snapshots;
    t_snapshot snapshots;
    t_atom buffer_in[MAX_LIST];
    int size_in;
    t_atom buffer_out[MAX_LIST];
    int size_out;
    int query_count;
    t_atom msg_buffer;
    t_signal_ref signals_in[MAX_LIST];
    t_signal_ref signals_out[MAX_LIST];
} t_implicitmap;

static t_symbol *ps_list;
static int port = 9000;

// *********************************************************
// -(function prototypes)-----------------------------------
static void *implicitmap_new(t_symbol *s, int argc, t_atom *argv);
static void implicitmap_free(t_implicitmap *x);
static void implicitmap_list(t_implicitmap *x, t_symbol *s, int argc, t_atom *argv);
static void implicitmap_poll(t_implicitmap *x);
static void implicitmap_randomize(t_implicitmap *x);
static void implicitmap_input_handler(mapper_signal msig, mapper_db_signal props, mapper_timetag_t *time, void *value);
static void implicitmap_query_handler(mapper_signal msig, mapper_db_signal props, mapper_timetag_t *time, void *value);
static void implicitmap_link_handler(mapper_db_link lnk, mapper_db_action_t a, void *user);
static void implicitmap_connect_handler(mapper_db_connection con, mapper_db_action_t a, void *user);
static void implicitmap_print_properties(t_implicitmap *x);
static int implicitmap_setup_mapper(t_implicitmap *x, const char *iface);
static void implicitmap_snapshot(t_implicitmap *x);
static void implicitmap_output_snapshot(t_implicitmap *x);
static void implicitmap_clear_snapshots(t_implicitmap *x);
static void implicitmap_mute_output(t_implicitmap *x, t_symbol *s, int argc, t_atom *argv);
static void implicitmap_process(t_implicitmap *x);
static void implicitmap_save(t_implicitmap *x);
static void implicitmap_load(t_implicitmap *x);
#ifdef MAXMSP
    void implicitmap_assist(t_implicitmap *x, void *b, long m, long a, char *s);
#endif
static void implicitmap_update_input_vector_positions(t_implicitmap *x);
static void implicitmap_update_output_vector_positions(t_implicitmap *x);
static const char *maxpd_atom_get_string(t_atom *a);
static void maxpd_atom_set_string(t_atom *a, const char *string);
static void maxpd_atom_set_int(t_atom *a, int i);
static double maxpd_atom_get_float(t_atom *a);
static void maxpd_atom_set_float(t_atom *a, float d);
void maxpd_atom_set_float_array(t_atom *a, float *d, int length);
static int osc_prefix_cmp(const char *str1, const char *str2, const char **rest);

// *********************************************************
// -(global class pointer variable)-------------------------
static void *mapper_class;

// *********************************************************
// -(main)--------------------------------------------------
#ifdef MAXMSP
int main(void)
    {
        t_class *c;
        c = class_new("implicitmap", (method)implicitmap_new, (method)implicitmap_free,
                      (long)sizeof(t_implicitmap), 0L, A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_assist,           "assist",    A_CANT,  0);
        class_addmethod(c, (method)implicitmap_snapshot,         "snapshot",  A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_randomize,        "randomize", A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_list,             "list",      A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_print_properties, "print",     A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_clear_snapshots,  "clear",     A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_mute_output,      "mute",      A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_process,          "process",   A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_save,             "export",      A_GIMME, 0);
        class_addmethod(c, (method)implicitmap_load,             "import",      A_GIMME, 0);
        class_register(CLASS_BOX, c); /* CLASS_NOBOX */
        mapper_class = c;
        ps_list = gensym("list");
        return 0;
    }
#else
    int implicitmap_setup(void)
    {
        t_class *c;
        c = class_new(gensym("implicitmap"), (t_newmethod)implicitmap_new, (t_method)implicitmap_free,
                      (long)sizeof(t_implicitmap), 0L, A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_snapshot,         gensym("snapshot"),  A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_randomize,        gensym("randomize"), A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_list,             gensym("list"),      A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_print_properties, gensym("print"),     A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_clear_snapshots,  gensym("clear"),     A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_mute_output,      gensym("mute"),      A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_process,          gensym("process"),   A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_save,             gensym("export"),      A_GIMME, 0);
        class_addmethod(c, (t_method)implicitmap_load,             gensym("import"),      A_GIMME, 0);
        mapper_class = c;
        ps_list = gensym("list");
        return 0;
    }
#endif

// *********************************************************
// -(new)---------------------------------------------------
void *implicitmap_new(t_symbol *s, int argc, t_atom *argv)
{
    t_implicitmap *x = NULL;
    long i;
    const char *alias = NULL;
    const char *iface = NULL;

#ifdef MAXMSP
    if (x = object_alloc(mapper_class)) {
        x->outlet3 = listout((t_object *)x);
        x->outlet2 = listout((t_object *)x);
        x->outlet1 = listout((t_object *)x);
#else
    if (x = (t_implicitmap *) pd_new(mapper_class) ) {
        x->outlet1 = outlet_new(&x->ob, gensym("list"));
        x->outlet2 = outlet_new(&x->ob, gensym("list"));
        x->outlet3 = outlet_new(&x->ob, gensym("list"));
#endif
        x->name = strdup("implicitmap");
        for (i = 0; i < argc; i++) {
            if ((argv + i)->a_type == A_SYM) {
                if(strcmp(maxpd_atom_get_string(argv+i), "@alias") == 0) {
                    if ((argv+i+1)->a_type == A_SYM) {
                        alias = maxpd_atom_get_string(argv+i+1);
                        i++;
                    }
                }
                else if(strcmp(maxpd_atom_get_string(argv+i), "@interface") == 0) {
                    if ((argv+i+1)->a_type == A_SYM) {
                        iface = maxpd_atom_get_string(argv+i+1);
                        i++;
                    }
                }
            }
        }

        if (alias) {
            free(x->name);
            x->name = *alias == '/' ? strdup(alias+1) : strdup(alias);
        }

        if (implicitmap_setup_mapper(x, iface)) {
            post("implcitmap: Error initializing.");
        }
        else {
            x->ready = 0;
            x->mute = 0;
            x->new_in = 0;
            x->query_count = 0;
            x->num_snapshots = 0;
            x->snapshots = 0;
            // initialize input and output buffers
            for (i = 0; i < MAX_LIST; i++) {
                maxpd_atom_set_float(x->buffer_in+i, 0);
                maxpd_atom_set_float(x->buffer_out+i, 0);
                x->signals_in[i].x = x;
                x->signals_out[i].x = x;
            }
            x->size_in = 0;
            x->size_out = 0;
#ifdef MAXMSP
            x->clock = clock_new(x, (method)implicitmap_poll);    // Create the timing clock
            x->timeout = clock_new(x, (method)implicitmap_output_snapshot);
#else
            x->clock = clock_new(x, (t_method)implicitmap_poll);
            x->timeout = clock_new(x, (t_method)implicitmap_output_snapshot);
#endif
            clock_delay(x->clock, INTERVAL);  // Set clock to go off after delay
        }
    }
    return (x);
}

// *********************************************************
// -(free)--------------------------------------------------
void implicitmap_free(t_implicitmap *x)
{
    if (x->clock) {
        clock_unset(x->clock);    // Remove clock routine from the scheduler
        clock_free(x->clock);     // Frees memory used by clock
    }
    if (x->device) {
        mdev_free(x->device);
    }
    if (x->db) {
        mapper_db_remove_connection_callback(x->db, implicitmap_connect_handler, x);
    }
    if (x->monitor) {
        mapper_monitor_free(x->monitor);
    }
    if (x->admin) {
        mapper_admin_free(x->admin);
    }
    if (x->name) {
        free(x->name);
    }
    implicitmap_clear_snapshots(x);
}

// *********************************************************
// -(print properties)--------------------------------------
void implicitmap_print_properties(t_implicitmap *x)
{
    if (x->ready) {
        //output name
        maxpd_atom_set_string(&x->msg_buffer, mdev_name(x->device));
        outlet_anything(x->outlet3, gensym("name"), 1, &x->msg_buffer);

        //output interface
        maxpd_atom_set_string(&x->msg_buffer, (char *)mdev_interface(x->device));
        outlet_anything(x->outlet3, gensym("interface"), 1, &x->msg_buffer);

        //output IP
        const struct in_addr *ip = mdev_ip4(x->device);
        if (ip) {
            maxpd_atom_set_string(&x->msg_buffer, inet_ntoa(*ip));
            outlet_anything(x->outlet3, gensym("IP"), 1, &x->msg_buffer);
        }

        //output port
        maxpd_atom_set_int(&x->msg_buffer, mdev_port(x->device));
        outlet_anything(x->outlet3, gensym("port"), 1, &x->msg_buffer);

        //output numInputs
        maxpd_atom_set_int(&x->msg_buffer, mdev_num_inputs(x->device) - 1);
        outlet_anything(x->outlet3, gensym("numInputs"), 1, &x->msg_buffer);

        //output numOutputs
        maxpd_atom_set_int(&x->msg_buffer, mdev_num_outputs(x->device) - 1);
        outlet_anything(x->outlet3, gensym("numOutputs"), 1, &x->msg_buffer);
    }
}

// *********************************************************
// -(inlet/outlet assist - maxmsp only)---------------------
#ifdef MAXMSP
void implicitmap_assist(t_implicitmap *x, void *b, long m, long a, char *s)
{
    if (m == ASSIST_INLET) { // inlet
        sprintf(s, "OSC input");
    }
    else {    // outlet
        if (a == 0) {
            sprintf(s, "Mapped OSC inputs");
        }
        else if (a == 1) {
            sprintf(s, "Snapshot data");
        }
        else {
            sprintf(s, "Device information");
        }
    }
}
#endif

// *********************************************************
// -(snapshot)----------------------------------------------
void implicitmap_snapshot(t_implicitmap *x)
{
    // if previous snapshot still in progress, output current snapshot status
    if (x->query_count) {
        post("still waiting for last snapshot");
        return;
    }

    int i;
    mapper_signal *psig;
    x->query_count = 0;

    // allocate a new snapshot
    if (x->ready) {
        t_snapshot new_snapshot = (t_snapshot)malloc(sizeof(t_snapshot));
        new_snapshot->id = x->num_snapshots++;
        new_snapshot->next = x->snapshots;
        new_snapshot->inputs = calloc(x->size_in, sizeof(float));
        new_snapshot->outputs = calloc(x->size_out, sizeof(float));
        x->snapshots = new_snapshot;
    }

    // iterate through input signals: hidden inputs correspond to
    // outputs/remote inputs - we need to query the remote end
    psig = mdev_get_inputs(x->device);
    for (i = 1; i < (mdev_num_inputs(x->device) + mdev_num_hidden_inputs(x->device)); i++) {
        mapper_db_signal props = msig_properties(psig[i]);
        if (props->hidden) {
            // find associated output
            mapper_signal output  = (mapper_signal) props->user_data;
            // query the remote value
            x->query_count += msig_query_remote(output, psig[i]);
        }
        else {
            void *value = msig_value(psig[i], 0);
            t_signal_ref *ref = props->user_data;
            int length = ref->offset + props->length < MAX_LIST ? props->length : MAX_LIST - ref->offset;
            // we can simply use memcpy here since all our signals are type 'f'
            memcpy(&x->snapshots->inputs[ref->offset], value, length*sizeof(float));
        }
    }
    if (x->query_count)
        clock_delay(x->timeout, 1000);  // Set clock to go off after delay
}

// *********************************************************
// -(snapshot)----------------------------------------------
void implicitmap_output_snapshot(t_implicitmap *x)
{
    if (x->query_count) {
        post("query timeout! setting query count to 0 and outputting current values.");        
        x->query_count = 0;
    }
    maxpd_atom_set_int(x->buffer_in, x->snapshots->id+1);
    outlet_anything(x->outlet3, gensym("numSnapshots"), 1, x->buffer_in);
    maxpd_atom_set_float_array(x->buffer_in, x->snapshots->inputs, x->size_in);
    outlet_anything(x->outlet2, gensym("in"), x->size_in, x->buffer_in);
    maxpd_atom_set_float_array(x->buffer_out, x->snapshots->outputs, x->size_out);
    outlet_anything(x->outlet2, gensym("out"), x->size_out, x->buffer_out);
    maxpd_atom_set_int(x->buffer_in, x->snapshots->id);
    outlet_anything(x->outlet2, gensym("snapshot"), 1, x->buffer_in);
}

// *********************************************************
// -(mute output)-------------------------------------------
void implicitmap_mute_output(t_implicitmap *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc) {
        if (argv->a_type == A_FLOAT)
            x->mute = (int)atom_getfloat(argv);
#ifdef MAXMSP
        else if (argv->a_type == A_LONG)
            x->mute = atom_getlong(argv);
#endif
    }
}

// *********************************************************
// -(process)-----------------------------------------------
void implicitmap_process(t_implicitmap *x)
{
    outlet_anything(x->outlet2, gensym("process"), 0, 0);
}

// *********************************************************
// -(save)--------------------------------------------------
void implicitmap_save(t_implicitmap *x)
{
    outlet_anything(x->outlet2, gensym("export"), 0, 0);
}

// *********************************************************
// -(load)--------------------------------------------------
void implicitmap_load(t_implicitmap *x)
{
    outlet_anything(x->outlet2, gensym("import"), 0, 0);
}

// *********************************************************
// -(randomize)---------------------------------------------
void implicitmap_randomize(t_implicitmap *x)
{
    int i, j;
    float rand_val;
    mapper_db_signal props;
    if (x->ready) {
        mapper_signal *psig = mdev_get_outputs(x->device);
        for (i = 1; i < mdev_num_outputs(x->device); i ++) {
            props = msig_properties(psig[i]);
            t_signal_ref *ref = props->user_data;
            if (props->type == 'f') {
                float v[props->length];
                for (j = 0; j < props->length; j++) {
                    rand_val = (float)rand() / (float)RAND_MAX;
                    if (props->minimum && props->maximum) {
                        v[j] = rand_val * (props->maximum->f - props->minimum->f) - props->minimum->f;
                    }
                    else {
                        // if ranges have not been declared, assume normalized between 0 and 1
                        v[j] = rand_val;
                    }
                    maxpd_atom_set_float(x->buffer_out+ref->offset+j, v[j]);
                }
                msig_update(psig[i], v);
            }
            else if (props->type == 'i') {
                int v[props->length];
                for (j = 0; j < props->length; j++) {
                    rand_val = (float)rand() / (float)RAND_MAX;
                    if (props->minimum && props->maximum) {
                        v[j] = (int) (rand_val * (props->maximum->i32 - props->minimum->i32) - props->minimum->i32);
                    }
                    else {
                        // if ranges have not been declared, assume normalized between 0 and 1
                        v[j] = (int) rand_val;
                    }
                    maxpd_atom_set_float(x->buffer_out+ref->offset+j, v[j]);
                }
                msig_update(psig[i], v);
            }
        }
        outlet_anything(x->outlet2, gensym("out"), x->size_out, x->buffer_out);
    }
}

// *********************************************************
// -(anything)----------------------------------------------
void implicitmap_list(t_implicitmap *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->mute)
        return;

    if (argc != x->size_out) {
        post("vector size mismatch");
        return;
    }

    int i=0, j=0;

    mapper_signal *psig = mdev_get_outputs(x->device);

    for (i=1; i < mdev_num_outputs(x->device); i++) {
        mapper_db_signal props = msig_properties(psig[i]);
        t_signal_ref *ref = props->user_data;
        if (props->type == 'f') {
            float v[props->length];
            for (j = 0; j < props->length; j++) {
                v[j] = atom_getfloat(argv+ref->offset+j);
            }
            msig_update(psig[i], v);
        }
        else if (props->type == 'i') {
            int v[props->length];
            for (j = 0; j < props->length; j++) {
                v[j] = (int)atom_getfloat(argv+ref->offset+j);
            }
            msig_update(psig[i], v);
        }
    }
    outlet_anything(x->outlet2, gensym("out"), argc, argv);
}

// *********************************************************
// -(input handler)-----------------------------------------
void implicitmap_input_handler(mapper_signal sig, mapper_db_signal props, mapper_timetag_t *time, void *value)
{
    t_signal_ref *ref = props->user_data;
    t_implicitmap *x = ref->x;
    if (!x)
        post("pointer problem! %i", x);

    int j;
    for (j=0; j < props->length; j++) {
        if (ref->offset+j >= MAX_LIST) {
            post("mapper: Maximum vector length exceeded!");
            break;
        }
        if (!value) {
            maxpd_atom_set_float(x->buffer_in+ref->offset+j, 0);
        }
        else if (props->type == 'f') {
            float *f = value;
            maxpd_atom_set_float(x->buffer_in+ref->offset+j, f[j]);
        }
        else if (props->type == 'i') {
            int *i = value;
            maxpd_atom_set_float(x->buffer_in+ref->offset+j, (float)i[j]);
        }
    }
    x->new_in = 1;
}

// *********************************************************
// -(query handler)-----------------------------------------
void implicitmap_query_handler(mapper_signal remote_sig, mapper_db_signal remote_props, mapper_timetag_t *time, void *value)
{
    mapper_signal local_sig = (mapper_signal) remote_props->user_data;

    mapper_db_signal local_props = msig_properties(local_sig);
    t_signal_ref *ref = local_props->user_data;
    t_implicitmap *x = ref->x;

    if (!local_props) {
        post("error in query_handler: user_data is NULL");
        return;
    }
    int j;

    for (j = 0; j < remote_props->length; j++) {
        if (ref->offset+j >= MAX_LIST) {
            post("mapper: Maximum vector length exceeded!");
            break;
        }
        if (!value)
            continue;
        else if (remote_props->type == 'f') {
            float *f = value;
            x->snapshots->outputs[ref->offset+j] = f[j];
        }
        else if (remote_props->type == 'i') {
            int *i = value;
            x->snapshots->outputs[ref->offset+j] = (float)i[j];
        }
    }

    x->query_count --;

    if (x->query_count == 0) {
        clock_unset(x->timeout);
        implicitmap_output_snapshot(x);
    }
}

// *********************************************************
// -(link handler)------------------------------------------
void implicitmap_link_handler(mapper_db_link lnk, mapper_db_action_t a, void *user_data)
{
    // do not allow self-links
    t_implicitmap *x = user_data;
    if (!x) {
        post("error in connect handler: user_data is NULL");
        return;
    }
    if (!x->ready) {
        post("error in connect handler: device not ready");
        return;
    }
    if (a == MDB_NEW) {
        if (strcmp(lnk->src_name, mdev_name(x->device)) == 0 &&
            strcmp(lnk->dest_name, mdev_name(x->device)) == 0) {
            mapper_monitor_unlink(x->monitor, lnk->src_name, lnk->dest_name);
        }
    }
}

// *********************************************************
// -(connection handler)------------------------------------
void implicitmap_connect_handler(mapper_db_connection con, mapper_db_action_t a, void *user)
{
    // if connected involves current generic signal, create a new generic signal
    t_implicitmap *x = user;
    if (!x) {
        post("error in connect handler: user_data is NULL");
        return;
    }
    if (!x->ready) {
        post("error in connect handler: device not ready");
        return;
    }
    const char *signal_name = 0;
    switch (a) {
        case MDB_NEW: {
            // check if applies to me
            if (!osc_prefix_cmp(con->src_name, mdev_name(x->device), &signal_name)) {
                if (strcmp(signal_name, con->dest_name) == 0)
                    return;
                if (mdev_num_outputs(x->device) >= MAX_LIST) {
                    post("Max outputs reached!");
                    return;
                }
                // disconnect the generic signal
                mapper_monitor_disconnect(x->monitor, con->src_name, con->dest_name);

                // add a matching output signal
                mapper_signal msig;
                char str[256];
                int length = con->dest_length ? : 1;
                msig = mdev_add_output(x->device, con->dest_name, length, 'f', 0,
                                       (con->range.known | CONNECTION_RANGE_DEST_MIN) ? &con->range.dest_min : 0,
                                       (con->range.known | CONNECTION_RANGE_DEST_MAX) ? &con->range.dest_max : 0);
                if (!msig) {
                    post("msig doesn't exist!");
                    return;
                }
                // connect the new signal
                msig_full_name(msig, str, 256);
                mapper_db_connection_t props;
                props.mode = MO_BYPASS;
                mapper_monitor_connect(x->monitor, str, con->dest_name, &props, CONNECTION_MODE);
                // add a corresponding hidden input signal for querying
                snprintf(str, 256, "%s%s", "/~", con->dest_name);
                mdev_add_hidden_input(x->device, str, con->dest_length,
                                      con->dest_type, 0, 0, 0,
                                      implicitmap_query_handler, msig);

                implicitmap_update_output_vector_positions(x);

                //output numOutputs
                maxpd_atom_set_int(&x->msg_buffer, mdev_num_outputs(x->device) - 1);
                outlet_anything(x->outlet3, gensym("numOutputs"), 1, &x->msg_buffer);
            }
            else if (!osc_prefix_cmp(con->dest_name, mdev_name(x->device), &signal_name)) {
                if (strcmp(signal_name, con->src_name) == 0)
                    return;
                if (mdev_num_inputs(x->device) >= MAX_LIST) {
                    post("Max inputs reached!");
                    return;
                }
                // disconnect the generic signal
                mapper_monitor_disconnect(x->monitor, con->src_name, con->dest_name);

                // create a matching input signal
                mapper_signal msig;
                char str[256];
                int length = con->src_length ? : 1;
                msig = mdev_add_input(x->device, con->src_name, length, 'f', 0,
                                      (con->range.known | CONNECTION_RANGE_SRC_MIN) ? &con->range.src_min : 0,
                                      (con->range.known | CONNECTION_RANGE_SRC_MAX) ? &con->range.src_max : 0,
                                      implicitmap_input_handler, x);
                if (!msig)
                    return;
                // connect the new signal
                mapper_db_connection_t props;
                props.mode = MO_BYPASS;
                msig_full_name(msig, str, 256);
                mapper_monitor_connect(x->monitor, con->src_name, str, &props, CONNECTION_MODE);

                implicitmap_update_input_vector_positions(x);

                //output numInputs
                maxpd_atom_set_int(&x->msg_buffer, mdev_num_inputs(x->device) - 1);
                outlet_anything(x->outlet3, gensym("numInputs"), 1, &x->msg_buffer);
            }
            break;
        }
        case MDB_MODIFY:
            break;
        case MDB_REMOVE: {
            mapper_signal msig;
            // check if applies to me
            if (!(osc_prefix_cmp(con->dest_name, mdev_name(x->device), &signal_name))) {
                if (strcmp(signal_name, "/CONNECT_HERE") == 0)
                    return;
                if (strcmp(signal_name, con->src_name) != 0)
                    return;
                // find corresponding signal
                if (!(msig = mdev_get_input_by_name(x->device, signal_name, 0))) {
                    post("error: input signal %s not found!", signal_name);
                    return;
                }
                // remove it
                mdev_remove_input(x->device, msig);
                implicitmap_update_input_vector_positions(x);

                //output numInputs
                maxpd_atom_set_int(&x->msg_buffer, mdev_num_inputs(x->device) - 1);
                outlet_anything(x->outlet3, gensym("numInputs"), 1, &x->msg_buffer);
            }
            else if (!(osc_prefix_cmp(con->src_name, mdev_name(x->device), &signal_name))) {
                char str[256];
                if (strcmp(signal_name, "/CONNECT_HERE") == 0)
                    return;
                if (strcmp(signal_name, con->dest_name) != 0)
                    return;
                // find corresponding signal
                if (!(msig = mdev_get_output_by_name(x->device, signal_name, 0))) {
                    post("error: output signal %s not found", signal_name);
                    return;
                }                
                // remove it
                mdev_remove_output(x->device, msig);

                // find corresponding hidden input signal
                snprintf(str, 256, "%s%s", "/~", signal_name);
                if (!(msig = mdev_get_input_by_name(x->device, str, 0))) {
                    post("error: hidden input signal %s not found", str);
                    return;
                }                
                // remove it
                mdev_remove_input(x->device, msig);

                implicitmap_update_output_vector_positions(x);

                //output numOutputs
                maxpd_atom_set_int(&x->msg_buffer, mdev_num_outputs(x->device) - 1);
                outlet_anything(x->outlet3, gensym("numOutputs"), 1, &x->msg_buffer);
            }
            break;
        }
    }
}

// *********************************************************
// -(compare signal names for qsort)------------------------
int compare_signal_names(const void *l, const void *r)
{
    mapper_db_signal l_props = msig_properties(*(mapper_signal*)l);
    mapper_db_signal r_props = msig_properties(*(mapper_signal*)r);
    return strcmp(l_props->name, r_props->name);
}

// *********************************************************
// -(set up new device and monitor)-------------------------
void implicitmap_update_input_vector_positions(t_implicitmap *x)
{
    int i, j=0, k=0, count;

    // store input signal pointers
    mapper_signal signals[mdev_num_inputs(x->device) - 1];
    mapper_signal *psig = mdev_get_inputs(x->device);
    // start counting at index 1 to ignore signal "/CONNECT_HERE"
    for (i = 1; i < mdev_num_inputs(x->device) + mdev_num_hidden_inputs(x->device); i++) {
        mapper_db_signal props = msig_properties(psig[i]);
        if (!props->hidden) {
            signals[j++] = psig[i];
        }
    }

    // sort input signal pointer array
    qsort(signals, mdev_num_inputs(x->device) - 1, sizeof(mapper_signal), compare_signal_names);

    // set offsets and user_data
    for (i = 0; i < mdev_num_inputs(x->device) - 1; i++) {
        mapper_db_signal props = msig_properties(signals[i]);
        x->signals_in[i].offset = k;
        props->user_data = &x->signals_in[i];
        k += props->length;
    }
    count = k < MAX_LIST ? k : MAX_LIST;
    if (count != x->size_in && x->num_snapshots) {
        post("implicitmap: input vector size has changed - resetting snapshots!");
        implicitmap_clear_snapshots(x);
    }
    x->size_in = count;
}

// *********************************************************
// -(set up new device and monitor)-------------------------
void implicitmap_update_output_vector_positions(t_implicitmap *x)
{
    int i, k=0, count;

    // store output signal pointers
    mapper_signal signals[mdev_num_outputs(x->device) - 1];
    mapper_signal *psig = mdev_get_outputs(x->device);
    // start counting at index 1 to ignore signal "/CONNECT_HERE"
    for (i = 1; i < mdev_num_outputs(x->device); i++) {
        signals[i-1] = psig[i];
    }

    // sort output signal pointer array
    qsort(signals, mdev_num_outputs(x->device) - 1, sizeof(mapper_signal), compare_signal_names);

    // set offsets and user_data
    for (i = 0; i < mdev_num_outputs(x->device) - 1; i++) {
        mapper_db_signal props = msig_properties(signals[i]);
        x->signals_out[i].offset = k;
        props->user_data = &x->signals_out[i];
        k += props->length;
    }
    count = k < MAX_LIST ? k : MAX_LIST;
    if (count != x->size_out && x->num_snapshots) {
        post("implicitmap: output vector size has changed - resetting snapshots!");
        implicitmap_clear_snapshots(x);
    }
    x->size_out = count;
}

// *********************************************************
// -(set up new device and monitor)-------------------------
int implicitmap_setup_mapper(t_implicitmap *x, const char *iface)
{
    post("using name: %s", x->name);
    x->admin = 0;
    x->device = 0;
    x->monitor = 0;
    x->db = 0;

    x->admin = mapper_admin_new(iface, 0, 0);
    if (!x->admin)
        return 1;

    x->device = mdev_new(x->name, port, x->admin);
    if (!x->device)
        return 1;

    x->monitor = mapper_monitor_new(x->admin, 0);
    if (!x->monitor)
        return 1;

    x->db = mapper_monitor_get_db(x->monitor);
    mapper_db_add_link_callback(x->db, implicitmap_link_handler, x);
    mapper_db_add_connection_callback(x->db, implicitmap_connect_handler, x);

    implicitmap_print_properties(x);

    return 0;
}

// *********************************************************
// -(poll libmapper)----------------------------------------
void implicitmap_poll(t_implicitmap *x)
{
    mdev_poll(x->device, 0);
    mapper_monitor_poll(x->monitor, 0);
    if (!x->ready) {
        if (mdev_ready(x->device)) {
            x->ready = 1;

            // create a new generic output signal
            mdev_add_output(x->device, "/CONNECT_HERE", 1, 'f', 0, 0, 0);

            // create a new generic input signal
            mdev_add_input(x->device, "/CONNECT_HERE", 1, 'f', 0, 0, 0, 0, x);

            implicitmap_print_properties(x);
        }
    }
    if (x->new_in) {
        outlet_anything(x->outlet1, gensym("list"), x->size_in, x->buffer_in);
        x->new_in = 0;
    }
    clock_delay(x->clock, INTERVAL);  // Set clock to go off after delay
}

// *********************************************************
// -(poll libmapper)----------------------------------------
void implicitmap_clear_snapshots(t_implicitmap *x)
{
    while (x->snapshots) {
        t_snapshot temp = x->snapshots->next;
        free(x->snapshots->inputs);
        free(x->snapshots->outputs);
        x->snapshots = temp;
    }
    x->num_snapshots = 0;
    outlet_anything(x->outlet2, gensym("clear"), 0, 0);
    maxpd_atom_set_int(x->buffer_in, 0);
    outlet_anything(x->outlet3, gensym("numSnapshots"), 1, x->buffer_in);
}

// *********************************************************
// some helper functions for abtracting differences
// between maxmsp and puredata

const char *maxpd_atom_get_string(t_atom *a)
{
#ifdef MAXMSP
    return atom_getsym(a)->s_name;
#else
    return (a)->a_w.w_symbol->s_name;
#endif
}

void maxpd_atom_set_string(t_atom *a, const char *string)
{
#ifdef MAXMSP
    atom_setsym(a, gensym((char *)string));
#else
    SETSYMBOL(a, gensym(string));
#endif
}

void maxpd_atom_set_int(t_atom *a, int i)
{
#ifdef MAXMSP
    atom_setlong(a, (long)i);
#else
    SETFLOAT(a, (double)i);
#endif
}

double maxpd_atom_get_float(t_atom *a)
{
    return (double)atom_getfloat(a);
}

void maxpd_atom_set_float(t_atom *a, float d)
{
#ifdef MAXMSP
    atom_setfloat(a, d);
#else
    SETFLOAT(a, d);
#endif
}

void maxpd_atom_set_float_array(t_atom *a, float *d, int length)
{
#ifdef MAXMSP
    atom_setfloat_array(length, a, length, d);
#else
    int i;
    for (i=0; i<length; i++) {
        SETFLOAT(a+i, d[i]);
    }
#endif
}

/* Helper function to check if the OSC prefix matches.  Like strcmp(),
 * returns 0 if they match (up to the second '/'), non-0 otherwise.
 * Also optionally returns a pointer to the remainder of str1 after
 * the prefix. */
static int osc_prefix_cmp(const char *str1, const char *str2,
                          const char **rest)
{
    if (str1[0]!='/') {
        return 0;
    }
    if (str2[0]!='/') {
        return 0;
    }

    // skip first slash
    const char *s1=str1+1, *s2=str2+1;

    while (*s1 && (*s1)!='/') s1++;
    while (*s2 && (*s2)!='/') s2++;

    int n1 = s1-str1, n2 = s2-str2;
    if (n1!=n2) return 1;

    if (rest)
        *rest = s1;

    return strncmp(str1, str2, n1);
}