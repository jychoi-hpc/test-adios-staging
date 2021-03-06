/*
 * ADIOS is freely available under the terms of the BSD license described
 * in the COPYING file in the top level directory of this source distribution.
 *
 * Copyright (c) 2008 - 2009.  UT-BATTELLE, LLC. All rights reserved.
 */

/* ADIOS ICEE Example
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <locale.h>
#include <algorithm>
#include "mpi.h"
#include "adios.h"
#include "adios_read.h"
#include "adios_error.h"
#include <string>
#include "icee_cmdline.h"
#include <sstream>
#include <time.h>
#include <sys/resource.h>
#include "filelock.h"

using namespace std;

#ifndef ATYPE
#define ATYPE       double
#endif
#ifndef ADIOS_ATYPE
#define ADIOS_ATYPE adios_double
#endif

#ifndef VERSION
#define VERSION NA
#endif
#define xstr(s) stringfy(s)
#define stringfy(s) #s

#define NVAR 1
#define SLEEP_SEC 3600

#include <sys/time.h>
struct timeval adios_timer_tp;
double adios_gettime()
{
    gettimeofday(&adios_timer_tp, NULL); \
    return  ((double)adios_timer_tp.tv_sec + ((double)adios_timer_tp.tv_usec)/1000000.0);
}

void sleep_with_interval (double timeout_sec, int interval_ms)
{
    int stay_in_poll_loop = 1;
    double t1 = adios_gettime();
    double elapsed_time;

    struct timespec delay;
    delay.tv_sec = interval_ms/1000;
    delay.tv_nsec = (((uint64_t)interval_ms * 1000000L)%1000000000L);

    while (stay_in_poll_loop)
    {
        elapsed_time = adios_gettime() - t1;

        if (elapsed_time > timeout_sec)
            stay_in_poll_loop = 0;
        else
            nanosleep(&delay, NULL);
    }
    //printf("Spent: %g (sec)\n", elapsed_time);
}

void do_define(const string adios_write_method, const string initstr, const string transformstr = "")
{
    int64_t       m_adios_group;

    // adios_flag_no to turn off statics
#if ADIOS_VERSION_LE(1, 10, 0)
    adios_declare_group (&m_adios_group, "restart", "", adios_flag_no);
#else
    adios_declare_group (&m_adios_group, "restart", "", adios_stat_no);
#endif
    adios_select_method (m_adios_group, adios_write_method.c_str(), initstr.c_str(), "");

    adios_define_var (m_adios_group, "NX",
                      "", adios_long,
                      0, 0, 0);

    adios_define_var (m_adios_group, "NY",
                      "", adios_long,
                      0, 0, 0);

    adios_define_var (m_adios_group, "G",
                      "", adios_long,
                      0, 0, 0);

    adios_define_var (m_adios_group, "O",
                      "", adios_long,
                      0, 0, 0);

    for (int i = 0; i < NVAR; i++)
    {
        char vname[32];
        sprintf(vname, "var%02d", i);
        int64_t var_id;
        var_id = adios_define_var (m_adios_group, vname,
                          "", ADIOS_ATYPE,
                          "NX,NY", "G,NY", "O,0");

        if (transformstr != "")
            adios_set_transform (var_id, transformstr.c_str());
    }

    adios_define_var (m_adios_group, "size",
                      "", adios_integer,
                      0, 0, 0);

    adios_define_var (m_adios_group, "rank",
                      "", adios_integer,
                      0, 0, 0);

    adios_define_var (m_adios_group, "timestamp",
                      "", adios_double,
                      "1", "size", "rank");
}

void do_write(const char* fname, const char* amode,
              uint64_t NX, uint64_t NY, const ATYPE *t,
              uint64_t G, uint64_t O,
              MPI_Comm world_comm,
              MPI_Comm comm,
              uint64_t *groupsize, double *t0, double *elap)
{
    int size, rank;
    MPI_Comm_rank(world_comm, &rank);
    MPI_Comm_size(world_comm, &size);

    int64_t     m_adios_file;
    uint64_t    adios_groupsize, adios_totalsize;

    double t1 = MPI_Wtime();
    adios_open (&m_adios_file, "restart", fname, amode, comm);
    adios_groupsize = 4 * 8 + NX * NY * sizeof(ATYPE) * NVAR;
    adios_group_size (m_adios_file, adios_groupsize, &adios_totalsize);
    //adios_set_max_buffer_size (adios_groupsize*size/1024L/1024L+1); // in MB

    double t2 = MPI_Wtime();
    adios_write(m_adios_file, "NX", (void *) &NX);
    adios_write(m_adios_file, "NY", (void *) &NY);
    adios_write(m_adios_file, "G", (void *) &G);
    adios_write(m_adios_file, "O", (void *) &O);
    for (int i = 0; i < NVAR; i++)
    {
        char vname[32];
        sprintf(vname, "var%02d", i);
        if (NX==0)
            adios_write(m_adios_file, vname, NULL);
        else
            adios_write(m_adios_file, vname, t);
    }

    adios_write(m_adios_file, "size", (void *) &size);
    adios_write(m_adios_file, "rank", (void *) &rank);
    adios_write(m_adios_file, "timestamp", (void *) &t1);

    double t3 = MPI_Wtime();
    adios_close (m_adios_file);
    double t4 = MPI_Wtime();

    //double t_elap14 = t4-t1;
    //double t_elap24 = t4-t2;
    //double t_elap34 = t4-t3;
    elap[0] = t4-t1;
    elap[1] = t4-t2;
    elap[2] = t4-t3;

    *groupsize = adios_groupsize;
    *t0 = t1;
}

void do_write_1var(const char* fname, const char* amode, const char* vname,
                   uint64_t NX, uint64_t NY, const ATYPE *t,
                   uint64_t G, uint64_t O,
                   MPI_Comm comm,
                   uint64_t *groupsize, double *t0, double *elap)
{
    int64_t     m_adios_file;
    uint64_t    adios_groupsize, adios_totalsize;

    double t1 = MPI_Wtime();
    adios_open (&m_adios_file, "restart", fname, amode, comm);
    adios_groupsize = 4 * 8 + NX * NY * sizeof(ATYPE);
    adios_group_size (m_adios_file, adios_groupsize, &adios_totalsize);
    //adios_set_max_buffer_size (adios_groupsize*size/1024L/1024L+1); // in MB

    double t2 = MPI_Wtime();
    adios_write(m_adios_file, "NX", (void *) &NX);
    adios_write(m_adios_file, "NY", (void *) &NY);
    adios_write(m_adios_file, "G", (void *) &G);
    adios_write(m_adios_file, "O", (void *) &O);
    adios_write(m_adios_file, vname, (void *) t);
    double t3 = MPI_Wtime();
    adios_close (m_adios_file);
    double t4 = MPI_Wtime();

    //double t_elap14 = t4-t1;
    //double t_elap24 = t4-t2;
    //double t_elap34 = t4-t3;
    elap[0] = t4-t1;
    elap[1] = t4-t2;
    elap[2] = t4-t3;

    *groupsize = adios_groupsize;
    *t0 = t1;
}

void do_read(ADIOS_FILE *f, ADIOS_VARINFO * v, uint64_t NX, uint64_t *start, uint64_t *count, int current_step,
             ATYPE *data, double* icee_deltat, double *t0, double* t1)
{
    ADIOS_SELECTION * sel;

    if (NX>0) sel = adios_selection_boundingbox (v->ndim, start, count);
  
    //MPI_Barrier(comm);
    *t0 = MPI_Wtime();
    
    if (NX>0) adios_schedule_read_byid (f, sel, v->varid, current_step, 1, data);
#ifdef ICEE
    adios_schedule_read (f, NULL, "__icee_deltat__", current_step, 1, icee_deltat);
#else
    *icee_deltat = 0.0;
#endif
    adios_perform_reads (f, 1);
    //printf("icee_deltat=%g\n", icee_deltat);
  
    //MPI_Barrier(comm);
    *t1 = MPI_Wtime();
  
    adios_release_step (f);
    if (NX>0) adios_selection_delete(sel);  
}

int main_leaf (int argc, char ** argv, MPI_Comm world_comm)
{
    printf("argc: %d\n", argc);
    int i = 0;
    while (argv[i]) {
        printf("argv[%d] = %s\n", i,argv[i]);
        i++;
    }

    setlinebuf(stdout);

    struct icee_args_info args_info;

    if (icee_cmdline_parser (argc, argv, &args_info) != 0)
        exit(1);

    if (args_info.version_given)
    {
        printf("%s (version: %s)\n", argv[0], xstr(VERSION));
        exit(1);
    }

    string adios_write_method = "POSIX1";
    if (args_info.client_flag) adios_write_method = "NULL";

    enum ADIOS_READ_METHOD adios_read_method = ADIOS_READ_METHOD_BP;

    uint64_t NX = args_info.len_arg;
    uint64_t NY = args_info.chunk_arg;
    uint64_t minlen = NX;
    if (args_info.all_flag)
        minlen = args_info.minlen_arg;
    //uint64_t NX_copy = args_info.len_arg;

    float timeout_sec = args_info.timeout_arg;
    int   interval_sec = args_info.sleep_arg;
    int   nstep = args_info.nstep_arg;
    if (nstep < 0)
        nstep = 1 << 30;

    if (args_info.writemethod_given)
        adios_write_method = string(args_info.writemethod_arg);
    std::transform(adios_write_method.begin(), adios_write_method.end(), adios_write_method.begin(), ::toupper);

    if (args_info.readmethod_given)
    {
        if (string(args_info.readmethod_arg) == "BP") {
            adios_read_method = ADIOS_READ_METHOD_BP;
        } else if (string(args_info.readmethod_arg) == "ICEE") {
            adios_read_method = ADIOS_READ_METHOD_ICEE;
        } else if (string(args_info.readmethod_arg) == "DIMES") {
            adios_read_method = ADIOS_READ_METHOD_DIMES;
        } else if (string(args_info.readmethod_arg) == "DATASPACES") {
            adios_read_method = ADIOS_READ_METHOD_DATASPACES;
        } else if (string(args_info.readmethod_arg) == "FLEXPATH") {
            adios_read_method = ADIOS_READ_METHOD_FLEXPATH;
        } else {
            fprintf(stderr, "No read method: %s\n", optarg);
        }
    }

    /*
     * Use the lock method for produer-consumer: https://en.wikipedia.org/wiki/Producer–consumer_problem
     */
    int use_lock = 0;
    if (args_info.uselock_flag)
        use_lock = 1;
    
    string lock[] = {"_write.lck", "_read.lck"};
    for (int i = 0; i < args_info.filelock_given; ++i)
    {
        lock[i] = string(args_info.filelock_arg[i]);
    }

    int seed = time(NULL);
    srand(seed);

    int world_rank, world_size;
    int rank, size, name_len;
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    MPI_Comm    comm;

    //MPI_Init(&argc, &argv);
    MPI_Comm_rank(world_comm, &world_rank);
    MPI_Comm_size(world_comm, &world_size);
    MPI_Get_processor_name(processor_name, &name_len);

    MPI_Comm_split(world_comm, args_info.mpicolor_arg, world_rank, &comm);
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    printf("world size and rank, comm size and comm rank, name = %d %d %d %d %s\n",
           world_size, world_rank, size, rank, processor_name);
    MPI_Barrier(world_comm);

    typedef enum {SERVER, CLIENT} mode_t;
    mode_t mode = SERVER;

    std::stringstream s;
    //time_t ltime;
    //char *timetext;

    if (args_info.client_flag)
        mode = CLIENT;

    string wparam = string(args_info.wparam_arg);
    string rparam = string(args_info.rparam_arg);
    string prefix = string(args_info.prefix_arg);

    {
        // common routine to set init string
        if (args_info.verbose_given)
            s << "verbose=" << args_info.verbose_arg << ";";

        if (args_info.host_given)
            s << "cm_host=" << args_info.host_arg << ";";

        s << "cm_port=" << args_info.port_arg + rank << ";";

        if (args_info.remotehost_given)
            s << "cm_remote_host=" << args_info.remotehost_arg << ";";

        s << "cm_remote_port=" << args_info.remoteport_arg + rank << ";";

        if (args_info.method_given)
            s << "transport=" << args_info.method_arg << ";";

        if (args_info.passive_given)
            s << "is_passive=" << args_info.passive_flag << ";";

        if (args_info.isnative_given)
            s << "use_native_contact=" << args_info.isnative_flag << ";";

        if (args_info.nclient_given)
            s << "max_client=" << args_info.nclient_arg << ";";

        if (args_info.remotelist_given)
            s << "remote_list=" << args_info.remotelist_arg << ";";

        if (args_info.attrlist_given)
            s << "attr_list=" << args_info.attrlist_arg << ";";

        if (args_info.probe_given)
            s << "use_probe=" << args_info.probe_flag << ";";

        if (args_info.allremotes_given)
            s << "cm_remote_host=" << args_info.allremotes_arg[rank % args_info.allremotes_given] << ";";
    }

    if (adios_write_method == "ICEE")
    {
        wparam = wparam + ";" + s.str();
    }

    if (adios_read_method == ADIOS_READ_METHOD_ICEE)
    {
        rparam = rparam + ";" + s.str();
    }

    setlocale(LC_NUMERIC, "en_US.UTF-8");

    string fname = "icee.bp";
    if (args_info.filename_given)
        fname = string(args_info.filename_arg);
    if (prefix.length() > 0)
        fname = prefix + "/" + fname;
    string fname_save = "icee_out.bp";

    adios_init_noxml (comm);

    uint64_t buffer_size = ((NX * NY * sizeof(ATYPE) + 64)>>20) + 1L + 500;
    printf("Allocating buffer: %lld\n", buffer_size);
    adios_set_max_buffer_size (buffer_size);

    do_define(adios_write_method, wparam, string(args_info.transform_arg));

    uint64_t adios_groupsize;//, adios_totalsize;
    uint64_t G, O;
    string   amode = "w";

    if (mode == SERVER)
    {
        if (use_lock && (rank == 0))
        {
            lockinit(lock[0].c_str(), 1);
            lockinit(lock[1].c_str(), 0);
        }
        
        while (NX >= minlen)
        {
            ATYPE        *t = (ATYPE *) malloc(NX * NY * sizeof(ATYPE));
            assert(t != NULL);

            //adios_allocate_buffer (ADIOS_BUFFER_ALLOC_NOW, ((NVAR * NX * NY * sizeof(ATYPE))>>20) + 1L);
            //adios_set_max_buffer_size (((NX * NY * sizeof(ATYPE) + 64)>>20) + 1L);

            G = NX * size;
            O = rank * NX;

            if (rank==0)
            {
                printf("===== SUMMARY =====\n");
                printf("%10s : %s\n", "Method", adios_write_method.c_str());
                printf("%10s : %s\n", "Params", wparam.c_str());
                printf("%10s : %'d (seconds)\n", "Interval", interval_sec);
                printf("%10s : %'d\n", "PEs", size);
                printf("%10s : %'llu x %'llu\n", "Local dims", NX, NY);
                printf("%10s : %'d\n", "NVar", NVAR);
                printf("%10s : %'.02f (MiB/proc)\n", "Data/PE", NVAR*NX*NY*sizeof(ATYPE)/1024.0/1024.0);
                printf("%10s : %'.02f (MiB)\n", "Total", NVAR*G*NY*sizeof(ATYPE)/1024.0/1024.0);
                printf("%10s : %'d\n", "Steps", nstep);
                printf("===================\n\n");
            }

            if (rank==0)
            {
                printf("    %14s %5s %5s %9s %9s %9s %9s %9s %9s\n", "timestep",   "seq",  "rank",   "t1(sec)",   "(MiB/s)",   "t2(sec)",   "(MiB/s)",   "t3(sec)",   "(MiB/s)");
                printf("    %14s %5s %5s %9s %9s %9s %9s %9s %9s\n", "--------", "-----", "-----", "---------", "---------", "---------", "---------", "---------", "---------");
            }
            MPI_Barrier(comm);
            
            for (int it =0; it < nstep; it++)
            {
                for (uint64_t i = 0; i < NX*NY; i++)
                    t[i] = rank + it + 1.0;

                if (args_info.append_flag)
                    amode = (it==0)? "w" : "a";

                if (use_lock && (rank == 0))
                {
                    lockdown(lock[0].c_str());
                }

                double t0, t_elap[3];
                MPI_Barrier(world_comm);
                if (args_info.commself_flag)
                    do_write(fname.c_str(), amode.c_str(), NX, NY, t, G, O, world_comm, MPI_COMM_SELF, &adios_groupsize, &t0, t_elap);
                else
                    do_write(fname.c_str(), amode.c_str(), NX, NY, t, G, O, world_comm, comm, &adios_groupsize, &t0, t_elap);

                if (use_lock && (rank == 0))
                {
                    lockup(lock[1].c_str());
                }
                
                //ltime=time(NULL);
                //timetext = asctime(localtime(&ltime));
                //timetext[24] = '\0';

                sleep_with_interval((double)interval_sec, SLEEP_SEC);

                printf(">>> %14.03f %5d %5d %9.03e %9.03f %9.03e %9.03f %9.03e %9.03f\n",
                       t0, it, rank,
                       t_elap[0], (double)adios_groupsize/t_elap[0]/1024.0/1024.0,
                       t_elap[1], (double)adios_groupsize/t_elap[1]/1024.0/1024.0,
                       t_elap[2], (double)adios_groupsize/t_elap[2]/1024.0/1024.0);

                MPI_Allreduce(MPI_IN_PLACE, &adios_groupsize, 1, MPI_LONG, MPI_SUM, world_comm);
                MPI_Allreduce(MPI_IN_PLACE, &t_elap, 3, MPI_DOUBLE, MPI_MAX, world_comm);
                if (rank==0)
                {
                    printf("SUM %14.03f %5d %5d %9.03e %9.03f %9.03e %9.03f %9.03e %9.03f\n",
                           t0, it, rank,
                           t_elap[0], (double)adios_groupsize/t_elap[0]/1024.0/1024.0,
                           t_elap[1], (double)adios_groupsize/t_elap[1]/1024.0/1024.0,
                           t_elap[2], (double)adios_groupsize/t_elap[2]/1024.0/1024.0);
                }
            }

            NX = NX/2;
        }
        adios_finalize (rank);
    }
    else
    {
        ADIOS_FILE * f;
        ADIOS_VARINFO * v;
        ADIOS_SELECTION * sel;
        //ADIOS_SELECTION * sel1;
        int err;

        ATYPE *data = NULL;
        uint64_t start[2], count[2];

        double icee_deltat = 0.0;
        uint64_t start1[2], count1[2];
        
        pid_t pid = getpid();

        err = adios_read_init_method (adios_read_method, comm, rparam.c_str());
        if (err != 0)
        {
            printf ("(%d) %s\n", err, adios_errmsg());
        }

        if (args_info.nostream_flag)
            f = adios_read_open_file (fname.c_str(), adios_read_method, comm);
        else
            f = adios_read_open (fname.c_str(), adios_read_method, comm,
                                 ADIOS_LOCKMODE_ALL, timeout_sec);

        if (adios_errno == err_file_not_found)
        {
            printf ("rank %d: Stream not found after waiting %f seconds: %s\n",
                    rank, timeout_sec, adios_errmsg());
        }
        else if (adios_errno == err_end_of_stream)
        {
            printf ("rank %d: Stream terminated before open. %s\n", rank, adios_errmsg());
        }
        else if (f == NULL) {
            printf ("rank %d: Error at opening stream: %s\n", rank, adios_errmsg());
        }
        else
        {
            while (1)
            {
                // Recover G from the metadata and compute NX and NY
                char vname[32];
                sprintf(vname, "var%02d", rand()%NVAR);
                v = adios_inq_var (f, vname);
                if (v != NULL)
                {
                    G = v->dims[0];
                    NX = G/size;
                    NY = v->dims[1];
                }
                else
                {
                    ADIOS_VARINFO *vg, *vy;
                    vg = adios_inq_var (f, "G");
                    vy = adios_inq_var (f, "NY");

                    G = *(uint64_t*)vg->value;
                    NX = G/size;
                    NY = *(uint64_t*)vy->value;

                    adios_free_varinfo(vg);
                    adios_free_varinfo(vy);
                }
                O = rank * NX;

                start[0] = rank * NX;
                count[0] = NX;
                start[1] = 0;
                count[1] = NY;

                if (rank == size-1)
                {
                    NX = G - rank * NX;
                    count[0] = NX;
                }

                start1[0] = rank;
                count1[0] = 1;

                if (args_info.evilread_flag)
                {
                    NX = args_info.len_arg;
                }

                //adios_allocate_buffer (ADIOS_BUFFER_ALLOC_NOW, ((NVAR * NX * NY * sizeof(ATYPE))>>20) + 1L);

                if (rank==0)
                {
                    printf("===== SUMMARY =====\n");
                    printf("%10s : %s\n", "Method", args_info.readmethod_arg);
                    printf("%10s : %s\n", "Params", rparam.c_str());
                    printf("%10s : %'d (seconds)\n", "Interval", interval_sec);
                    printf("%10s : %'d\n", "PEs", size);
                    printf("%10s : %'llu x %'llu\n", "Dims", G, NY);
                    printf("%10s : %'llu x %'llu\n", "Local dims", NX, NY);
                    printf("%10s : %'d\n", "Steps", nstep);
                    printf("%10s : %s\n", "WMethod", adios_write_method.c_str());
                    printf("%10s : %s\n", "WParams", wparam.c_str());
                    printf("===================\n\n");
                }

                data = (ATYPE *) malloc (NX * NY * sizeof(ATYPE));
                assert(data != NULL);

                if (rank==0)
                {
                    printf("%3s %14s %5s %5s %9s %9s %9s %9s (%9s %9s %9s)\n", "+++", "timestep",   "seq",  "rank", "time(sec)",   "(MiB/s)", "dT", "(MiB/s)", "Check", "", "");
                    printf("%3s %14s %5s %5s %9s %9s %9s %9s  %9s %9s %9s\n", "+++", "--------", "-----", "-----", "---------", "---------", "---------", "---------", "---------", "---------", "---------");
                }

                /* Processing loop over the steps (we are already in the first one) */
                //while (adios_errno != err_end_of_stream) {
                int current_step = 0;
                for (int it =0; it < nstep; it++)
                {
                    /*
                     * Evil read
                     * -. Continuously read with random offsets
                     * -. NX is set by len argument
                     */
                EVIL:
                    if (use_lock && (rank == 0) && !(args_info.evilread_flag))
                    {
                        lockdown(lock[1].c_str());
                    }
                    
                    if (args_info.evilread_flag)
                    {
                        O = rand()%(G - NX);
                        start[0] = O;
                        count[0] = NX;
                    }
                    double t0=0.0, t1= 0.0, t10=0.0;
                    do_read(f, v, NX, start, count, current_step, data, &icee_deltat, &t0, &t1);
                    t10 = t1 - t0;
                    /*
                    if (NX>0) sel = adios_selection_boundingbox (v->ndim, start, count);
                    sel1 = adios_selection_boundingbox (1, start1, count1);

                    //MPI_Barrier(comm);
                    double t0 = MPI_Wtime();

                    if (NX>0) adios_schedule_read_byid (f, sel, v->varid, current_step, 1, data);
                    adios_schedule_read (f, NULL, "__icee_deltat__", current_step, 1, &icee_deltat);
                    adios_perform_reads (f, 1);
                    //printf("icee_deltat=%g\n", icee_deltat);

                    //MPI_Barrier(comm);
                    double t1 = MPI_Wtime();
                    double t10 = t1 - t0;

                    adios_release_step (f);
                    if (NX>0) adios_selection_delete(sel);
                    adios_selection_delete(sel1);
                    */

                    if (use_lock && (rank == 0) && !(args_info.evilread_flag))
                    {
                        lockup(lock[0].c_str());
                    }
                    
                    double sum = 0.0;
                    for (uint64_t i = 0; i < NX * NY; i++)
                    {
                        sum += (double) data[i];
                    }

                    adios_groupsize = (NX*NY)*sizeof(ATYPE);
                    printf("+++ (%d) %14.03f %5d %5d %9.03e %9.03f %9.03e %9.03f (%9s %9.03e %9.03e)\n",
                           pid, t0, f->current_step, rank, t10,
                           (double)adios_groupsize/t10/1024.0/1024.0,
                           icee_deltat, (double)adios_groupsize/icee_deltat/1024.0/1024.0,
                           vname/*f->var_namelist[v->varid]*/, sum, sum/NX/NY);
                    //printf("%d: groupsize = %llu, %f %f\n", rank, adios_groupsize, (double)adios_groupsize/deltat, sum_deltat/(double)cnt_deltat);

                    MPI_Allreduce(MPI_IN_PLACE, &adios_groupsize, 1, MPI_LONG, MPI_SUM, world_comm);
                    MPI_Allreduce(MPI_IN_PLACE, &t10, 1, MPI_DOUBLE, MPI_MAX, world_comm);
                    MPI_Allreduce(MPI_IN_PLACE, &icee_deltat, 1, MPI_DOUBLE, MPI_MAX, world_comm);
                    if (rank==0)
                    {
                        printf("SUM %14.03f %5d %5d %9.03e %9.03f %9.03e %9.03f\n",
                               t0, f->current_step, rank,
                               t10, (double)adios_groupsize/t10/1024.0/1024.0,
                               icee_deltat, (double)adios_groupsize/icee_deltat/1024.0/1024.0);
                    }

                    if (args_info.evilread_flag)
                    {
                        int vid = rand()%NVAR;
                        //char vname[32];
                        sprintf(vname, "var%02d", vid);
                        v = adios_inq_var (f, vname);
                        goto EVIL;
                    }

                    // Save what is read
                    if (args_info.append_flag)
                        amode = (it==0)? "w" : "a";

                    if (adios_write_method != "NULL")
                    {
                        double t_elap[3];
                        MPI_Barrier(world_comm);
                        do_write_1var(fname_save.c_str(), amode.c_str(), vname/*f->var_namelist[v->varid]*/,
                                      NX, NY, data, G, O, comm, &adios_groupsize, &t0, t_elap);

                        if (it==0 && rank==0)
                        {
                            printf("    %14s %5s %5s %9s %9s %9s %9s %9s %9s\n", "timestep",   "seq",  "rank",   "t1(sec)",   "(MiB/s)",   "t2(sec)",   "(MiB/s)",   "t3(sec)",   "(MiB/s)");
                            printf("    %14s %5s %5s %9s %9s %9s %9s %9s %9s\n", "--------", "-----", "-----", "---------", "---------", "---------", "---------", "---------", "---------");
                        }

                        printf(">>> %14.03f %5d %5d %9.03e %9.03f %9.03e %9.03f %9.03e %9.03f\n",
                               t0, f->current_step, rank,
                               t_elap[0], (double)adios_groupsize/t_elap[0]/1024.0/1024.0,
                               t_elap[1], (double)adios_groupsize/t_elap[1]/1024.0/1024.0,
                               t_elap[2], (double)adios_groupsize/t_elap[2]/1024.0/1024.0);
                    }
                    if (it==nstep-1) break;

                    sleep_with_interval((double)interval_sec, SLEEP_SEC);

                    // advance to 1) next available step with 2) blocking wait
                    if (args_info.nostream_flag)
                    {
                        current_step++;
                        adios_read_close (f);
                        f = adios_read_open_file (fname.c_str(), adios_read_method, comm);
                    }
                    else
                        adios_advance_step (f, 0, timeout_sec);
                    
                    if (adios_errno == err_step_notready)
                    {
                        printf ("rank %d: No new step arrived within the timeout. Quit. %s\n",
                                rank, adios_errmsg());
                        break; // quit while loop
                    }

                    if (adios_errno == err_end_of_stream)
                    {
                        printf ("rank %d: End of stream. Quit. %s\n",
                                rank, adios_errmsg());
                        break; // quit while loop
                    }
                }

                if (args_info.all_flag && (NX>minlen))
                {
                    printf ("Waiting next ... (NX=%lld)\n", NX);

                    sleep_with_interval((double)interval_sec, SLEEP_SEC);

                    // advance to 1) next available step with 2) blocking wait
                    adios_advance_step (f, 0, timeout_sec);
                    if (adios_errno == err_step_notready)
                    {
                        printf ("rank %d: No new step arrived within the timeout. Quit. %s\n",
                                rank, adios_errmsg());
                        break; // quit while loop
                    }

                    if (adios_errno == err_end_of_stream)
                    {
                        printf ("rank %d: End of stream. Quit. %s\n",
                                rank, adios_errmsg());
                        break; // quit while loop
                    }
                }
                else
                    break;
            }

            adios_read_close (f);
            free (data);
        }

        adios_read_finalize_method (adios_read_method);
    }

    MPI_Barrier(comm);
    if (rank==0)
    {
        printf ("Done.\n");

        struct rusage r_usage;
        getrusage(RUSAGE_SELF,&r_usage);
        printf("Memory usage: %'.2f MiB\n", r_usage.ru_maxrss/1024.0/1024.0);

        void *v = NULL;
        int vval=-1, flag=-1;
        MPI_Comm_get_attr(world_comm, MPI_WTIME_IS_GLOBAL, &v, &flag);
        if (flag)
            vval = *(int*)v;
        printf("MPI_WTIME_IS_GLOBAL: %d\n", vval);
    }

    return 0;
}

int main (int argc, char ** argv)
{
    MPI_Init(&argc, &argv);
    int rc = main_leaf(argc, argv, MPI_COMM_WORLD);
    MPI_Finalize ();
    return rc;
}
