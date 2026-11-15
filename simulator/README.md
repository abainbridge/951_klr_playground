This simulator can run the firware from the original KLR ROM image.

It generates enough sensor inputs well enough to make the firmware think it is
running in a real car. This works by having a very simple model of the car
(engine, DME, turbo).

Press 'h' to be show keyboard controls.

Limitations:

* Spark angle is fixed at 30 degrees before TDC.

* The cycling valve isn't modelled, so doesn't effect boost pressure.

* The knock self test isn't modelled.