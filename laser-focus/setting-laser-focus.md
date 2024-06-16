# Setting laser focus

K40 laser cutter comes with a focal length of 50.8 mm. So in theory, your bed level should be good if your bed is 50.8 mm away from the laser lens.

There is a good video you can watch here : <https://www.youtube.com/watch?v=u4YHbDBBofg&t=301s>

* Make your bed level. To do that mesure the distance from the nut to top part of the bolt.
The distance must be the same on the four bed corners.

* Select "Move to bed origin" on the control panel menu. Your bed will go down to the origin.

* Use a piece of plywood and a block in order to make a ramp.

* Use `Inkscape` to draw a 20 cm line. Set the outline in blue (Recognized as Laser engrave in `Whisperer`)

* Use `Whisperer` to laser engrave the plywood (20 mm/s at 5mA, those settings can be ajusted)

* Find the spot where the line is the narrowest

* Use a rule to measure the distance between where the line is the narrowest and the laser head

* On the control panel menu, select `Move from origin to bed focal` and use `Bed offset from origin to focal` to adjust the distance from the bed to the laser head with the distance measured just before

* Save your settings : from main menu `Settings`, and `Save settings`

* You're done !
