from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import unittest
import numpy as np
import time
import ray

class ActorAPI(unittest.TestCase):

  def testKeywordArgs(self):
    ray.init(num_workers=0, driver_mode=ray.SILENT_MODE)

    @ray.actor
    class Actor(object):
      def __init__(self, arg0, arg1=1, arg2="a"):
        self.arg0 = arg0
        self.arg1 = arg1
        self.arg2 = arg2
      def get_values(self, arg0, arg1=2, arg2="b"):
        return self.arg0 + arg0, self.arg1 + arg1, self.arg2 + arg2

    actor = Actor(0)
    self.assertEqual(ray.get(actor.get_values(1)), (1, 3, "ab"))

    actor = Actor(1, 2)
    self.assertEqual(ray.get(actor.get_values(2, 3)), (3, 5, "ab"))

    actor = Actor(1, 2, "c")
    self.assertEqual(ray.get(actor.get_values(2, 3, "d")), (3, 5, "cd"))

    # Make sure we get an exception if the constructor is called incorrectly.
    actor = Actor()
    with self.assertRaises(Exception):
      ray.get(ray.get(actor.get_values(1)))
    with self.assertRaises(Exception):
      ray.get(ray.get(actor.get_values()))

    # Make sure we get an exception if the method is called incorrectly.
    actor = Actor(1)
    with self.assertRaises(Exception):
      ray.get(ray.get(actor.get_values()))

    ray.worker.cleanup()

  def testVariableNumberOfArgs(self):
    ray.init(num_workers=0)

    @ray.actor
    class Actor(object):
      def __init__(self, arg0, arg1=1, *args):
        self.arg0 = arg0
        self.arg1 = arg1
        self.args = args
      def get_values(self, arg0, arg1=2, *args):
        return self.arg0 + arg0, self.arg1 + arg1, self.args, args

    actor = Actor(0)
    self.assertEqual(ray.get(actor.get_values(1)), (1, 3, (), ()))

    actor = Actor(1, 2)
    self.assertEqual(ray.get(actor.get_values(2, 3)), (3, 5, (), ()))

    actor = Actor(1, 2, "c")
    self.assertEqual(ray.get(actor.get_values(2, 3, "d")), (3, 5, ("c",), ("d",)))

    actor = Actor(1, 2, "a", "b", "c", "d")
    self.assertEqual(ray.get(actor.get_values(2, 3, 1, 2, 3, 4)), (3, 5, ("a", "b", "c", "d"), (1, 2, 3, 4)))

    ray.worker.cleanup()

  def testNoArgs(self):
    ray.init(num_workers=0)

    @ray.actor
    class Actor(object):
      def __init__(self):
        pass
      def get_values(self):
        pass

    actor = Actor()
    self.assertEqual(ray.get(actor.get_values()), None)

    ray.worker.cleanup()

  def testNoConstructor(self):
    # If no __init__ method is provided, that should not be a problem.
    ray.init(num_workers=0)

    @ray.actor
    class Actor(object):
      def get_values(self):
        pass

    actor = Actor()
    self.assertEqual(ray.get(actor.get_values()), None)

    ray.worker.cleanup()

  def testCustomClasses(self):
    ray.init(num_workers=0)

    class Foo(object):
      def __init__(self, x):
        self.x = x
    ray.register_class(Foo)

    @ray.actor
    class Actor(object):
      def __init__(self, f2):
        self.f1 = Foo(1)
        self.f2 = f2
      def get_values1(self):
        return self.f1, self.f2
      def get_values2(self, f3):
        return self.f1, self.f2, f3

    actor = Actor(Foo(2))
    results1 = ray.get(actor.get_values1())
    self.assertEqual(results1[0].x, 1)
    self.assertEqual(results1[1].x, 2)
    results2 = ray.get(actor.get_values2(Foo(3)))
    self.assertEqual(results2[0].x, 1)
    self.assertEqual(results2[1].x, 2)
    self.assertEqual(results2[2].x, 3)

    ray.worker.cleanup()

  # def testCachingActors(self):
  #   # TODO(rkn): Implement this.
  #   pass

class ActorMethods(unittest.TestCase):

  def testDefineActor(self):
    ray.init()

    @ray.actor
    class Test(object):
      def __init__(self, x):
        self.x = x
      def f(self, y):
        return self.x + y

    t = Test(2)
    self.assertEqual(ray.get(t.f(1)), 3)

    ray.worker.cleanup()

  def testActorState(self):
    ray.init()

    @ray.actor
    class Counter(object):
      def __init__(self):
        self.value = 0
      def increase(self):
        self.value += 1
      def value(self):
        return self.value

    c1 = Counter()
    c1.increase()
    self.assertEqual(ray.get(c1.value()), 1)

    c2 = Counter()
    c2.increase()
    c2.increase()
    self.assertEqual(ray.get(c2.value()), 2)

    ray.worker.cleanup()

  def testMultipleActors(self):
    # Create a bunch of actors and call a bunch of methods on all of them.
    ray.init(num_workers=0)

    @ray.actor
    class Counter(object):
      def __init__(self, value):
        self.value = value
      def increase(self):
        self.value += 1
        return self.value
      def reset(self):
        self.value = 0

    num_actors = 20
    num_increases = 50
    # Create multiple actors.
    actors = [Counter(i) for i in range(num_actors)]
    results = []
    # Call each actor's method a bunch of times.
    for i in range(num_actors):
      results += [actors[i].increase() for _ in range(num_increases)]
    result_values = ray.get(results)
    for i in range(num_actors):
      self.assertEqual(result_values[(num_increases * i):(num_increases * (i + 1))], list(range(i + 1, num_increases + i + 1)))

    # Reset the actor values.
    [actor.reset() for actor in actors]

    # Interweave the method calls on the different actors.
    results = []
    for j in range(num_increases):
      results += [actor.increase() for actor in actors]
    result_values = ray.get(results)
    for j in range(num_increases):
      self.assertEqual(result_values[(num_actors * j):(num_actors * (j + 1))], num_actors * [j + 1])

    ray.worker.cleanup()

class ActorNesting(unittest.TestCase):

  def testRemoteFunctionWithinActor(self):
    # Make sure we can use remote funtions within actors.
    ray.init(num_cpus=100)

    # Create some values to close over.
    val1 = 1
    val2 = 2

    @ray.remote
    def f(x):
      return val1 + x

    @ray.remote
    def g(x):
      return ray.get(f.remote(x))

    @ray.actor
    class Actor(object):
      def __init__(self, x):
        self.x = x
        self.y = val2
        self.object_ids = [f.remote(i) for i in range(5)]
        self.values2 = ray.get([f.remote(i) for i in range(5)])

      def get_values(self):
        return self.x, self.y, self.object_ids, self.values2

      def f(self):
        return [f.remote(i) for i in range(5)]

      def g(self):
        return ray.get([g.remote(i) for i in range(5)])

      def h(self, object_ids):
        return ray.get(object_ids)

    actor = Actor(1)
    values = ray.get(actor.get_values())
    self.assertEqual(values[0], 1)
    self.assertEqual(values[1], val2)
    self.assertEqual(ray.get(values[2]), list(range(1, 6)))
    self.assertEqual(values[3], list(range(1, 6)))

    self.assertEqual(ray.get(ray.get(actor.f())), list(range(1, 6)))
    self.assertEqual(ray.get(actor.g()), list(range(1, 6)))
    self.assertEqual(ray.get(actor.h([f.remote(i) for i in range(5)])), list(range(1, 6)))

    ray.worker.cleanup()

  def testDefineActorWithinActor(self):
    # Make sure we can use remote funtions within actors.
    ray.init()

    @ray.actor
    class Actor1(object):
      def __init__(self, x):
        self.x = x

      def new_actor(self, z):
        @ray.actor
        class Actor2(object):
          def __init__(self, x):
            self.x = x
          def get_value(self):
            return self.x
        self.actor2 = Actor2(z)

      def get_values(self, z):
        self.new_actor(z)
        return self.x, ray.get(self.actor2.get_value())

    actor1 = Actor1(3)
    self.assertEqual(ray.get(actor1.get_values(5)), (3, 5))

    ray.worker.cleanup()

  # TODO(rkn): The test testUseActorWithinActor currently fails with a pickling
  # error.
  # def testUseActorWithinActor(self):
  #   # Make sure we can use remote funtions within actors.
  #   ray.init()
  #
  #   @ray.actor
  #   class Actor1(object):
  #     def __init__(self, x):
  #       self.x = x
  #     def get_val(self):
  #       return self.x
  #
  #   @ray.actor
  #   class Actor2(object):
  #     def __init__(self, x, y):
  #       self.x = x
  #       self.actor1 = Actor1(y)
  #
  #     def get_values(self, z):
  #       return self.x, ray.get(self.actor1.get_val())
  #
  #   actor2 = Actor2(3, 4)
  #   self.assertEqual(ray.get(actor2.get_values(5)), (3, 4))
  #
  #   ray.worker.cleanup()

  def testDefineActorWithinRemoteFunction(self):
    # Make sure we can define and actors within remote funtions.
    ray.init()

    @ray.remote
    def f(x, n):
      @ray.actor
      class Actor1(object):
        def __init__(self, x):
          self.x = x
        def get_value(self):
          return self.x
      actor = Actor1(x)
      return ray.get([actor.get_value() for _ in range(n)])

    self.assertEqual(ray.get(f.remote(3, 1)), [3])
    self.assertEqual(ray.get([f.remote(i, 20) for i in range(10)]), [20 * [i] for i in range(10)])

    ray.worker.cleanup()

  # This test currently fails with a pickling error.
  # def testUseActorWithinRemoteFunction(self):
  #   # Make sure we can create and use actors within remote funtions.
  #   ray.init()
  #
  #   @ray.actor
  #   class Actor1(object):
  #     def __init__(self, x):
  #       self.x = x
  #     def get_values(self):
  #       return self.x
  #
  #   @ray.remote
  #   def f(x):
  #     actor = Actor1(x)
  #     return ray.get(actor.get_values())
  #
  #   self.assertEqual(ray.get(f.remote(3)), 3)
  #
  #   ray.worker.cleanup()

  def testActorImportCounter(self):
    # This is mostly a test of the export counters to make sure that when an
    # actor is imported, all of the necessary remote functions have been
    # imported.
    ray.init()

    # Export a bunch of remote functions.
    num_remote_functions = 50
    for i in range(num_remote_functions):
      @ray.remote
      def f():
        return i

    @ray.remote
    def g():
      @ray.actor
      class Actor(object):
        def __init__(self):
          # This should use the last version of f.
          self.x = ray.get(f.remote())
        def get_val(self):
          return self.x
      actor = Actor()
      return ray.get(actor.get_val())

    self.assertEqual(ray.get(g.remote()), num_remote_functions - 1)

    ray.worker.cleanup()

class ActorInheritance(unittest.TestCase):

  def testInheritActorFromClass(self):
    # Make sure we can define an actor by inheriting from a regular class. Note
    # that actors cannot inherit from other actors.
    ray.init()

    class Foo(object):
      def __init__(self, x):
        self.x = x
      def f(self):
        return self.x
      def g(self, y):
        return self.x + y

    @ray.actor
    class Actor(Foo):
      def __init__(self, x):
        Foo.__init__(self, x)
      def get_value(self):
        return self.f()

    actor = Actor(1)
    self.assertEqual(ray.get(actor.get_value()), 1)
    self.assertEqual(ray.get(actor.g(5)), 6)

    ray.worker.cleanup()

class ActorSchedulingProperties(unittest.TestCase):

  def testRemoteFunctionsNotScheduledOnActors(self):
    # Make sure that regular remote functions are not scheduled on actors.
    ray.init(num_workers=0)

    @ray.actor
    class Actor(object):
      def __init__(self):
        pass

    actor = Actor()

    @ray.remote
    def f():
      return 1

    # Make sure that f cannot be scheduled on the worker created for the actor.
    # The wait call should time out.
    ready_ids, remaining_ids = ray.wait([f.remote() for _ in range(10)], timeout=3000)
    self.assertEqual(ready_ids, [])
    self.assertEqual(len(remaining_ids), 10)

    ray.worker.cleanup()

class ActorsOnMultipleNodes(unittest.TestCase):

  def testActorLoadBalancing(self):
    num_local_schedulers = 3
    ray.worker._init(start_ray_local=True, num_workers=0, num_local_schedulers=num_local_schedulers)

    @ray.actor
    class Actor1(object):
      def __init__(self):
        pass
      def get_location(self):
        return ray.worker.global_worker.plasma_client.store_socket_name

    # Create a bunch of actors.
    num_actors = 30
    actors = [Actor1() for _ in range(num_actors)]

    # Make sure that actors are spread between the local schedulers.
    locations = ray.get([actor.get_location() for actor in actors])
    names = set(locations)
    self.assertEqual(len(names), num_local_schedulers)
    self.assertTrue(all([locations.count(name) > 5 for name in names]))

    # Make sure we can get the results of a bunch of tasks.
    results = []
    for _ in range(1000):
      index = np.random.randint(num_actors)
      results.append(actors[index].get_location())
    ray.get(results)

    ray.worker.cleanup()

if __name__ == "__main__":
  unittest.main(verbosity=2)
