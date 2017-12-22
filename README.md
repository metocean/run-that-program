# docker-init
A INIT / PID 1 program that starts your app written in C that does:
- that passes signals handed to PID 1 to the main program.
- allows remapping of signals passed to the main program.
- reaps zombie processes.
- allows for an initial program to be run before the main program.
- allows for an exit program to be run after the main program has exited.

- [docker-init](#docker-init)
  * [usage](#usage)
  * [example](#example)
  * [on docker stop / SIGTERM](#on-docker-stop)
  * [on docker kill -s SIGNAL](#on-docker-kill-signal)
  * [docker signals](#docker-signals)
  * [c make](#c-make)
  * [Dockerfile example](#dockerfile-example)

## usage
```
usage: docker-init --map [from-sig] [to-sig] --init [program / args ..] --program [program / args ..]

  --map [from-sig] [to-sig]: this re-maps a signal received by docker-init app to the program, you can have more than one mapping

  --program [norm program args]: this is the program + it args to be run in the docker

  --on-start [init program args]: the init program runs first, before consul and --program. If it returns nonzero consul-init will exit.

  --after-exit [exit program args]: the exit program runs after the main program as stop.

examples:

  docker-init --on-start echo hello --program sleep 2 --after-exit echo goodbye

  docker-init --map TERM QUIT --program /bin/nginx -g daemon off;

  docker-init --map TERM QUIT --on-start wget http://[somesite]/config.json --program /bin/nginx -g daemon off;

```

## example
this will start nginx and the consul agent. When ```docker stop``` is used nginx will gractefully shutdown and consul will deregister the service.
```
/bin/consul-init --map TERM QUIT --program /bin/nginx -g daemon off;
```
```--map``` maps the terminate signal to quit allowing nginx to gracefully shut down.
```
/bin/consul-init --map TERM QUIT --init wget http://[somesite]/config.json --program /bin/nginx -g daemon off;
```
```--init``` will run wget before the nginx or consul-agent. If wget exits none zero consul-init will exit without running consul agent or the program nginx.

## on docker stop
on docker stop / SIGTERM or on SIGINT consul-init will:
1. stop the consul agent greacefully allowing it to redregister itself.
2. send a SIGTERM to the program, or if the user has mapped TERM to another signal it will send the mapped [to-signal].

## on docker kill SIGNAL
on docker kill -s SIGNAL consul-init will send the SIGNAL to the program, or if the user has mapped SIGNAL to another signal it will send the mapped [to-signal].

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
# install consul-init
ENV DOCKER_INIT_VERSION=0.0.1
RUN echo "----------------- install consul-init -----------------" &&\
    cd /tmp &&\
    curl -o docker-init.tar.gz -L https://github.com/metocean/docker-init/archive/v${DOCKER_INIT_VERSION}.tar.gz &&\
    tar -vxf docker-init.tar.gz &&\
    cd /tmp/docker-init-${DOCKER_INIT_VERSION}/docker-init &&\
    make &&\
    cp docker-init /bin/ &&\    
    cd /tmp && rm -rf /tmp/docker-init

ENTRYPOINT ["docker-init", "--program", "gunicorn", "mywebapp", "--bind", "0.0.0.0:80"]
```
