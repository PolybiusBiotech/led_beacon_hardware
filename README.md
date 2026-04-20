\# LED Beacon



An LED Beacon.



\## Specification



Interfaces:

* Ideally DMX512 (5pin XLR) and Art-Net (RJ45)

  * Want DMX to be robust so aim for 5kV isolated transceiver, TVS diodes and failsafe biasing
* At least one, slight DMX512 preference
* +12V DC input
* weatherproof connections
* 10x 6.4V 0.3A LED strips



Control:

* Treat as 10x lights per beacon
* be able to set normal brightness for each 'light'
* but fast enough for strobe effect for each 'light'
* Use a modern microcontroller



Misc:

* JLC assembly
* Target quantity is 25
* Target cost is \~£15-20 per assembled PCBA



\## Repo Structure



* /hardware : KiCAD schematics and board layout
* /firmware : firmware for LED Beacon
* /docs : calculations, diagrams and outputs



\## Licence



&#x20;To be decided

