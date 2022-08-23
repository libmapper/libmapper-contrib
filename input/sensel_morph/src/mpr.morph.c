#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "sensel.h"
#include "sensel_device.h"
#include "mapper/mapper.h"

const char *default_name = "morph";
SENSEL_HANDLE handle = NULL;
SenselDeviceList list;
SenselSensorInfo sensor_info;
SenselFrameData *frame = NULL;
unsigned int last_n_contacts = 0;
int verbose = 1;
int done = 0;

mpr_dev dev;
mpr_sig num_contacts;
mpr_sig acceleration;
mpr_sig position;
mpr_sig area;
mpr_sig force;
mpr_sig orientation;
mpr_sig axes;
mpr_sig velocity;
mpr_time time;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void loop()
{
    unsigned int n_frames = 0;

    while (!done) {
        senselReadSensor(handle);
        senselGetNumAvailableFrames(handle, &n_frames);

        for (int f = 0; f < n_frames; f++) {
            senselGetFrame(handle, frame);

            if (!frame->n_contacts && !last_n_contacts)
                continue;
            else if (frame->n_contacts != last_n_contacts)
                eprintf("num_contacts: %d\n", frame->n_contacts);

            mpr_sig_set_value(num_contacts, 0, 1, MPR_INT32, &frame->n_contacts);
            mpr_sig_set_value(acceleration, 0, 3, MPR_INT32, &frame->accel_data);

            for (int c = 0; c < frame->n_contacts; c++) {
                SenselContact sc = frame->contacts[c];
                unsigned int state = sc.state;
                register int id = (int)sc.id;

                switch (state) {
                    case CONTACT_START:
                    case CONTACT_MOVE:
                        mpr_sig_set_value(position, id, 2, MPR_FLT, &sc.x_pos);
                        mpr_sig_set_value(velocity, id, 2, MPR_FLT, &sc.delta_x);
                        mpr_sig_set_value(orientation, id, 1, MPR_FLT, &sc.orientation);
                        mpr_sig_set_value(axes, id, 2, MPR_FLT, &sc.major_axis);
                        mpr_sig_set_value(force, id, 1, MPR_FLT, &sc.total_force);
                        mpr_sig_set_value(area, id, 1, MPR_FLT, &sc.area);
                        break;
                    default:
                        eprintf("releasing instance %d\n", id);
                        mpr_sig_release_inst(position, id);
                        mpr_sig_release_inst(velocity, id);
                        mpr_sig_release_inst(orientation, id);
                        mpr_sig_release_inst(axes, id);
                        mpr_sig_release_inst(force, id);
                        mpr_sig_release_inst(area, id);
                        break;
                }
            }
            mpr_dev_update_maps(dev);
            last_n_contacts = frame->n_contacts;
        }
        mpr_dev_poll(dev, 0);
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, connected = 0;
    const char *dev_name = default_name;
    signal(SIGINT, ctrlc);

    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("mpr.morph.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-h help, "
                               "--alias <string> (default: '%s')\n", default_name);
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case '-':
                        if (++j < len && strcmp(argv[i]+j, "alias")==0)
                            if (++i < argc)
                                dev_name = argv[i];
                        break;
                    default:
                        break;
                }
            }
        }
    }

    // create libmapper device
    eprintf("Joining mapping graph... ");
    dev = mpr_dev_new(dev_name, NULL);
    while (!done && !mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 100);
    }
    eprintf(" registered!\n");

    // add signals
    int mini[2] = {0, 0}, maxi[2] = {16, 16};
    num_contacts = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/num_contacts",
                               1, MPR_INT32, NULL, mini, maxi, NULL, NULL, 0);
    // TODO check these ranges!
    float minf[2] = {0.f, 0.f}, maxf[2] = {1.0f, 1.0f};
    acceleration = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/acceleration",
                               3, MPR_INT32, "G", minf, maxf, NULL, NULL, 0);
    int num_inst = 16;
    maxf[0] = 240;
    maxf[1] = 139;
    position = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/contact/position",
                           2, MPR_FLT, "mm", minf, maxf, &num_inst, NULL, 0);
    maxf[0] = 33360;
    area = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/contact/area", 1, MPR_FLT,
                       "mm^2", minf, maxf, &num_inst, NULL, 0);
    maxf[0] = 400;
    force = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/contact/force", 1,
                        MPR_FLT, NULL, minf, maxf, &num_inst, NULL, 0);
    maxf[0] = 360;
    orientation = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/contact/orientation",
                              1, MPR_FLT, "degrees", minf, maxf, &num_inst, NULL, 0);
    maxf[0] = 240;
    maxf[1] = 139;
    axes = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/contact/axes", 2, MPR_FLT,
                       "mm", minf, maxf, &num_inst, NULL, 0);
    velocity = mpr_sig_new(dev, MPR_DIR_OUT, "instrument/contact/velocity", 2,
                           MPR_FLT, "mm/sec", NULL, NULL, &num_inst, NULL, 0);

    // connect to Sensel Morph
    eprintf("Looking for Sensel Morph device...\n");
    while (!done) {
        senselGetDeviceList(&list);
        if (list.num_devices) {
            connected = 1;
            break;
        }
        mpr_dev_poll(dev, 100);
    }
    if (!connected)
        goto done;

    // open connection to Sensel Morph
    senselOpenDeviceByID(&handle, list.devices[0].idx);
    // setup for contact and accelerometer data
    senselSetFrameContent(handle, FRAME_CONTENT_CONTACTS_MASK | FRAME_CONTENT_ACCEL_MASK);
    // pre-allocate a frame of data
    senselAllocateFrameData(handle, &frame);
    // start scanning
    senselStartScanning(handle);

    // force the serial connection to remain open
    // https://forum.sensel.com/t/pausing-e-g-in-a-debugger-causes-all-subsequent-apis-to-fail/219/15
    unsigned char val[1] = { 255 };
    senselWriteReg(handle, 0xD0, 1, val);

    loop();

    // allow serial connection to close
    val[0] = 0;
    senselWriteReg(handle, 0xD0, 1, val);
    senselClose(handle);

done:
    // unregister from mapping graph
    mpr_dev_free(dev);
    return 0;
}
