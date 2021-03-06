# Using Ray on a cluster

Deploying Ray on a cluster currently requires a bit of manual work.

## Deploying Ray on a cluster.

This section assumes that you have a cluster running and that the node in the
cluster can communicate with each other. It also assumes that Ray is installed
on each machine. To install Ray, follow the instructions for [installation on
Ubuntu](install-on-ubuntu.md).

### Starting Ray on each machine.

On the head node (just choose some node to be the head node), run the following,
replacing `<redis-port>` with a port of your choice, e.g., `6379`.

```
./ray/scripts/start_ray.sh --head --redis-port <redis-port>
```

The `--redis-port` arugment is optional, and if not provided Ray starts Redis
on a port selected at random.
In either case, the command will print out the address of the Redis server
that was started (and some other address information).

Then on all of the other nodes, run the following. Make sure to replace
`<redis-address>` with the value printed by the command on the head node (it
should look something like `123.45.67.89:6379`).

```
./ray/scripts/start_ray.sh --redis-address <redis-address>
```

To specify the number of processes to start, use the flag `--num-workers`, as
follows:

```
./ray/scripts/start_ray.sh --num-workers <int>
```

Now we've started all of the Ray processes on each node Ray. This includes

- Some worker processes on each machine.
- An object store on each machine.
- A local scheduler on each machine.
- One Redis server (on the head node).
- One global scheduler (on the head node).
- Optionally, this may start up some processes for visualizing the system state
  through a web UI.

To run some commands, start up Python on one of the nodes in the cluster, and do
the following.

```python
import ray
ray.init(redis_address="<redis-address>")
```

Now you can define remote functions and execute tasks. For example:

```python
@ray.remote
def f(x):
  return x

ray.get([f.remote(f.remote(f.remote(0))) for _ in range(1000)])
```

### Stopping Ray

When you want to stop the Ray processes, run `./ray/scripts/stop_ray.sh`
on each node.

### Using the Web UI on a Cluster

If you followed the instructions for setting up the web UI, then
`./ray/scripts/start_ray.sh --head` will attempt to start a Python 3 webserver
on the head node. In order to view the web UI from a machine that is not part of
the cluster (like your laptop), you can use SSH port forwarding. The web UI
requires ports 8080 and 8888, which you can forward using a command like the
following.

```
ssh -L 8080:localhost:8080 -L 8888:localhost:8888 ubuntu@<head-node-public-ip>
```

Then you can view the web UI on your laptop by navigating to
`http://localhost:8080` in a browser.

#### Troubleshooting the Web UI

Note that to use the web UI, additional setup is required. In particular, you
must be using Python 3 or you must at least have `python3` on your path.

If the web UI doesn't work, it's possible that the web UI processes were never
started (check `ps aux | grep polymer` and `ps aux | grep ray_ui.py`). If they
were started, see if you can fetch the web UI from within the head node (try
`wget http://localhost:8080` on the head node).
