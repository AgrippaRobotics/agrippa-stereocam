/*
 * cmd_calib_solve.cpp — `calib-solve` entry point.
 *
 * Separate TU from calib_solve.cpp so the solve is callable as a library (the backend
 * will want it in-process) without dragging in argument parsing.
 */

#ifdef HAVE_OPENCV

#include "calib_solve.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../vendor/argtable3.h"
}

extern "C" int cmd_calib_solve (int argc, char *argv[], arg_dstr_t res, void *ctx);

int
cmd_calib_solve (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) res; (void) ctx;
    AgCalibSolveOpts o;
    const char *session = NULL, *out = "";
    /* argtable3 hands the subcommand its own name in argv; depending on how it is
     * invoked that lands at argv[0] or argv[1], so skip it wherever it is rather
     * than assuming, or it gets consumed as the session path. */
    for (int i = 1; i < argc; i++) {
        if (!strcmp (argv[i], "calib-solve")) continue;
        if (!strcmp (argv[i], "--out") && i + 1 < argc)             out = argv[++i];
        else if (!strcmp (argv[i], "--alpha") && i + 1 < argc)      o.alpha = atof (argv[++i]);
        else if (!strcmp (argv[i], "--keep-outliers"))              o.keep_outliers = true;
        else if (!strcmp (argv[i], "--refine-intrinsics"))          o.refine_intrinsics = true;
        else if (!strcmp (argv[i], "--quiet"))                      o.verbose = false;
        else if (!strcmp (argv[i], "--columns") && i + 1 < argc)    o.spec.columns = atoi (argv[++i]);
        else if (!strcmp (argv[i], "--rows") && i + 1 < argc)       o.spec.rows = atoi (argv[++i]);
        else if (!strcmp (argv[i], "--tag-mm") && i + 1 < argc)     o.spec.tag_mm = atof (argv[++i]);
        else if (!strcmp (argv[i], "--gap-mm") && i + 1 < argc)     o.spec.gap_mm = atof (argv[++i]);
        else if (argv[i][0] != '-' && !session)                     session = argv[i];
        else {
            fprintf (stderr, "usage: calib-solve <session> [--out DIR] [--alpha A]\n"
                             "       [--keep-outliers] [--refine-intrinsics] [--quiet]\n"
                             "       [--columns N] [--rows N] [--tag-mm MM] [--gap-mm MM]\n");
            return 2;
        }
    }
    if (!session) {
        fprintf (stderr, "calib-solve: no session directory given\n");
        return 2;
    }
    return ag_calib_solve (session, out, o) == 0 ? 0 : 1;
}

#endif /* HAVE_OPENCV */
