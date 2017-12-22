# run-that --program
A INIT / PID 1 program that starts your app written in C that does:
- that passes signals handed to PID 1 to the main program.
- allows remapping of signals passed to the main program.
- reaps zombie processes.
- allows for an initial program to be run before the main program.
- allows for an exit program to be run after the main program has exited.

- [run-that](#run-that)
  * [usage](#usage)
  * [example](#example)
  * [on docker stop / SIGTERM](#on-docker-stop)
  * [on docker kill -s SIGNAL](#on-docker-kill-signal)
  * [docker signals](#docker-signals)
  * [c make](#c-make)
  * [Dockerfile example](#dockerfile-example)

## usage
```
 usage: run-that --map [from-sig] [to-sig] --on-start [program / args ..] --program [program / args ..] --after-exit [program / args ..] 

   --map [from-sig] [to-sig]: this re-maps a signal received by run-that app to the program, you can have more than one mapping

   --program [norm program args]: this is the program + it args to be run in the docker

   --on-start [start program args]: the start program runs first, before --program. 

   --after-exit [exit program args]: the exit program runs after the --program as exited. 

 examples: 

   ./run-that --on-start echo hello --program sleep 2 --after-exit echo goodbye 

   ./run-that --map TERM QUIT --program /bin/nginx -g daemon off;

   ./run-that --map TERM QUIT --on-start wget http://[somesite]/config.json --program /bin/nginx -g daemon off;

```

## example
this will start nginx and the consul agent. When ```docker stop``` is used nginx will gractefully shutdown and consul will deregister the service.
```
/bin/docker-init --map TERM QUIT --program /bin/nginx -g daemon off;
```
```--map``` maps the terminate signal to quit allowing nginx to gracefully shut down.
```
/bin/docker-init --map TERM QUIT --on-start wget http://[somesite]/config.json --program /bin/nginx -g daemon off;
```
```--on-start``` will run wget before the main program nginx.

## on docker stop
on docker stop / SIGTERM or on SIGINT docker-init will:
1. send a SIGTERM to the program, or if the user has mapped TERM to another signal it will send the mapped [to-signal].

## on docker kill SIGNAL
on docker kill -s SIGNAL docker-init will send the SIGNAL to the program, or if the user has mapped SIGNAL to another signal it will send the mapped [to-signal].

## docker signals
* ```docker stop```: The main process inside the container will receive SIGTERM, and after a grace period (default 10 seconds), SIGKILL.
* ```docker kill -s SIGNAL```: will send a singal to the process in the container.

## c make
```
cd docker-init
make
make clean
```

## Dockerfile example
```
# install run-that
ENV RUN_THAT_VERSION=0.0.1
RUN echo "----------------- install consul-init -----------------" &&\
    cd /tmp &&\
    curl -o run-that.tar.gz -L https://github.com/metocean/docker-run-that/archive/v${RUN_THAT_VERSION}.tar.gz &&\
    tar -vxf run-that.tar.gz &&\
    cd /tmp/run-that-${DOCKER_INIT_VERSION}/run-that &&\
    make &&\
    cp run-that /bin/ &&\    
    cd /tmp && rm -rf /tmp/run-that

ENTRYPOINT ["run-that", "--program", "gunicorn", "mywebapp", "--bind", "0.0.0.0:80"]
```
