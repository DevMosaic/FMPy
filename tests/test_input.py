import numpy as np
from fmpy.simulation import Input
from fmpy.model_description import ModelDescription, ScalarVariable

inf = float('Inf')


def test_single_sample():
    t = np.array([0])
    y = np.array([2])

    # "interpolate" input with only one sample
    u, du = Input.interpolate(1, t, y)

    assert u == 2
    assert du == 0

def test_input_continuous():

    t = np.array( [ 0, 1, 2, 3])
    y = np.array([[ 0, 0, 3, 3],
                  [-1, 0, 1, 2]])

    # extrapolate left (hold)
    (u1, u2), (du1, du2) = Input.interpolate(-1, t, y)
    assert (u1, u2) == (0, -1)
    assert (du1, du2) == (0, 0)

    # hit sample
    (u1, u2), (du1, du2) = Input.interpolate(1, t, y)
    assert (u1, u2) == (0, 0)
    assert (du1, du2) == (0, 1)

    # interpolate (linear)
    (u1, u2), (du1, du2) = Input.interpolate(1.5, t, y)
    assert (u1, u2) == (1.5, 0.5)
    assert (du1, du2) == (3, 1)

    # extrapolate right (hold)
    (u1, u2), (du1, du2) = Input.interpolate(4, t, y)
    assert (u1, u2) == (3, 2)
    assert (du1, du2) == (0, 0)

def test_continuous_signal_events():

    dtype = np.dtype([('time', np.float64)])

    model_description = ModelDescription()

    # no event
    signals = np.array([(0,), (1,)], dtype=dtype)
    t_events = Input.findEvents(signals, model_description)
    assert [inf] == t_events

    # time grid with events at 0.5 and 0.8
    signals = np.array(list(zip([0.1, 0.2, 0.3, 0.4, 0.5, 0.5, 0.6, 0.7, 0.8, 0.8, 0.8, 0.9, 1.0])), dtype=dtype)
    t_events = Input.findEvents(signals, model_description)
    assert np.all([0.5, 0.8, inf] == t_events)

def test_discrete_signal_events():

    # model with one discrete variable 'x'
    model_description = ModelDescription()
    variable = ScalarVariable('x', 0)
    variable.variability = 'discrete'
    model_description.modelVariables.append(variable)

    # discrete events at 0.1 and 0.4
    signals = np.array([
        (0.0, 0),
        (0.1, 0),
        (0.2, 1),
        (0.3, 1),
        (0.4, 2)],
        dtype=np.dtype([('time', np.float64), ('x', int)]))

    t_event = Input.findEvents(signals, model_description)

    assert np.all([0.2, 0.4, inf] == t_event)

def test_input_discrete():

    t = np.array( [0, 1, 1, 1, 2])
    y = np.array([[0, 0, 4, 3, 3]])

    # extrapolate left
    u, du = Input.interpolate(-1, t, y)
    assert u == 0, "Expecting first value"
    assert du == 0

    # hit sample
    u, du = Input.interpolate(0, t, y)
    assert u == 0, "Expecting value at sample"
    assert du == 0

    # interpolate
    u, du = Input.interpolate(0.5, t, y)
    assert u == 0, "Expecting to hold previous value"
    assert du == 0

    # before event
    u, du = Input.interpolate(1, t, y)
    assert u == 0, "Expecting value before event"
    assert du == 0

    # after event
    u, du = Input.interpolate(1, t, y, after_event=True)
    assert u == 3, "Expecting value after event"
    assert du == 0

    # extrapolate right
    u, du = Input.interpolate(3, t, y)
    assert u == 3, "Expecting last value"
    assert du == 0
