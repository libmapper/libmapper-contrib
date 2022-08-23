/* Adaptation of MacBook Multitouch code by Erling Ellingsen:       *
 * http://www.steike.com/code/multitouch/                           */

#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mapper/mapper.h>

#define NUMTOUCHES 16

typedef struct { float x,y; } mtPoint;
typedef struct { mtPoint pos,vel; } mtReadout;

typedef struct {
    int frame;
    double timestamp;
    int identifier, state, foo3, foo4;
    mtReadout normalized;
    float size;
    int zero1;
    float angle, majorAxis, minorAxis; // ellipsoid
    mtReadout mm;
    int zero2[2];
    float unk2;
} Finger;

typedef void *MTDeviceRef;
typedef int (*MTContactCallbackFunction)(int,Finger*,int,double,int);

MTDeviceRef MTDeviceCreateDefault();
void MTRegisterContactFrameCallback(MTDeviceRef, MTContactCallbackFunction);
void MTDeviceStart(MTDeviceRef, int); // thanks comex

const char* default_name = "touchpad";
mpr_dev mdev = 0;
mpr_sig countSig = 0;
mpr_sig angleSig = 0;
mpr_sig ellipseSig = 0;
mpr_sig positionSig = 0;
mpr_sig velocitySig = 0;
mpr_sig areaSig = 0;

int done = 0;
int verbose = 1;
int unpolled = 0;

int callback(int device, Finger *data, int nFingers, double timestamp, int frame) {
    float pair[2];

    if (verbose)
        printf("Touch count: %d\n", nFingers);
    else {
        printf("\rTouch count: %d ", nFingers);
        fflush(stdout);
    }

    mpr_sig_set_value(countSig, 0, 1, MPR_INT32, &nFingers);

    Finger *f = &data[0];
    for (int i = 0; i < nFingers; i++) {
        Finger *f = &data[i];
        if (verbose)
            printf("  ID %2d, Angle %4.2f, ellipse %5.2f x%5.2f, "
                   "position %5.3f, %5.3f, vel %6.3f, %6.3f, area %6.3f\n",
                   f->identifier,
                   f->angle,
                   f->majorAxis,
                   f->minorAxis,
                   f->normalized.pos.x,
                   f->normalized.pos.y,
                   f->normalized.vel.x,
                   f->normalized.vel.y,
                   f->size);
        if (f->size > 0) {
            // update libmapper signals
            mpr_sig_set_value(angleSig, f->identifier, 1, MPR_FLT, &f->angle);

            pair[0] = f->majorAxis;
            pair[1] = f->minorAxis;
            mpr_sig_set_value(ellipseSig, f->identifier, 2, MPR_FLT, pair);

            pair[0] = f->normalized.pos.x;
            pair[1] = f->normalized.pos.y;
            mpr_sig_set_value(positionSig, f->identifier, 2, MPR_FLT, pair);

            pair[0] = f->normalized.vel.x;
            pair[1] = f->normalized.vel.y;
            mpr_sig_set_value(velocitySig, f->identifier, 2, MPR_FLT, pair);

            mpr_sig_set_value(areaSig, f->identifier, 1, MPR_FLT, &f->size);

            if (f->size > 1) {
                // calculate pan/rotate/zoom

                // pan = avg velocity scaled by current zoom level

                // zoom = average expansion/contraction around mutual centre
            }
        }
        else {
            mpr_sig_release_inst(angleSig, f->identifier);
            mpr_sig_release_inst(ellipseSig, f->identifier);
            mpr_sig_release_inst(positionSig, f->identifier);
            mpr_sig_release_inst(velocitySig, f->identifier);
            mpr_sig_release_inst(areaSig, f->identifier);
        }
    }
    mpr_dev_update_maps(mdev);
    return 0;
}

void add_signals()
{
    int mini = 0, num_touches = NUMTOUCHES;
    countSig = mpr_sig_new(mdev, MPR_DIR_OUT, "touch/count", 1, MPR_INT32, NULL,
                           &mini, &num_touches, NULL, 0, 0);

    float minf[2] = {0.f, 0.f};
    float maxf[2] = {M_PI, M_PI};
    angleSig = mpr_sig_new(mdev, MPR_DIR_OUT, "touch/angle", 1, MPR_FLT, "radians",
                           minf, maxf, &num_touches, 0, 0);

    ellipseSig = mpr_sig_new(mdev, MPR_DIR_OUT, "touch/ellipse", 2, MPR_FLT,
                             "mm", 0, 0, &num_touches, 0, 0);

    minf[0] = 0.f; minf[1] = 0.f;
    maxf[0] = 1.f; maxf[1] = 1.f;
    positionSig = mpr_sig_new(mdev, MPR_DIR_OUT, "touch/position", 2, MPR_FLT,
                              "normalized", minf, maxf, &num_touches, 0, 0);

    velocitySig = mpr_sig_new(mdev, MPR_DIR_OUT, "touch/velocity", 2, MPR_FLT,
                              "normalized", 0, 0, &num_touches, 0, 0);

    areaSig = mpr_sig_new(mdev, MPR_DIR_OUT, "touch/area", 1, MPR_FLT,
                          0, 0, 0, &num_touches, 0, 0);
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j;
    const char* dev_name = default_name;
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("macbook_trackpad_mapper.c: possible arguments "
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

    signal(SIGINT, ctrlc);

    mdev = mpr_dev_new(dev_name, 0);
    add_signals();
    while (!mpr_dev_get_is_ready(mdev)) {
        mpr_dev_poll(mdev, 0);
    }

    MTDeviceRef dev = MTDeviceCreateDefault();
    MTRegisterContactFrameCallback(dev, callback);
    MTDeviceStart(dev, 0);
    printf("Ctrl-C to abort\n");

    // use non-blocking poll() to avoid being interrupted frequently by the touch callback
    while (!done) {
        mpr_dev_poll(mdev, 0);
        usleep(10000);
    }

    printf("freeing mapper device... ");
    mpr_dev_free(mdev);
    printf("done.\n");
    return 0;
}
