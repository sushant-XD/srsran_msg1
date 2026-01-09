MSG1 Flooding Attack
======
This repository modifies the existing srsRAN library to flood PRACH messages which causes gNodeB to overwhelm and 
not respond to the legitimate UE's PRACH message. 

How It Works
============
Configuration for msg1 attack (example provided in `srsue/ue.conf`) should be passed while running, in addition to the regular configuration used 
for UE. 

Some background:
MSG1 or PRACH message doesn't contain any special message, information or data, its just a preamble made of Zadoff Chu sequences. Hence its really short.
And gNodeB only listens for PRACH symbols on specified slots.

There are a total of 64 PRACH preambles. The preambles have very low cross-correlation. The UE gets PRACH config from SIB1 and randomly selects a preamble 
index. And when the UE sends PRACH message and doesn't receive Random Access Response (RAR) back in a certain time (RA window), it sends another PRACH with a higher
power, ramping up the power everytime by a fixed amount until the RAR is received or until the max transmission threshold is met. 

The way gNodeB detects the PRACH message is by correlation-based detection, comparing peak magnitude to a detection threshold. The threshold is adapted
dynamically based on Noise floor. And since the preambles have very low cross-correlation, multiple preambles can overlap and gNodeB can still detect 
multiple ones. And PRACH is detectable at very low SNR. PRACH detection is basically matched filtering, meaning a weak legitimate preamble can be detected
even if the signal power from UE is less than the Noise at that channel. And gain controls in the receiver side makes sure that it can detect signal saturation.

Approach:
This repository approaches to flood the gNodeB with malicious and dynamically-configured msg1 so that the gNodeB's Automatic Gain Control (AGC) adjusts itself 
and the gNodeB isn't able to detect legitimate UEs PRACH messages. In order to make our generated PRACH preambles look "normal" to gNodeB, a random PRACH symbol 
is selected, and sent to gNodeB. Instead of receiving RAR however, power is ramped up and PRACH message is sent in every PRACH occassion until the power ceiling
is reached, acting like the UE is not able detect RAR. This cycle is continuously repeated. Hence, gNodeB's Automatic Gain Control changes so that lower
power PRACH messages aren't received, and receiver is desensitized. And the Correlation threshold for PRACH messages increases as well. 

Sending multiple preambles at the same time increases the effect significantly. However, sending many preambles concurrently from same hardware (more than 3)
actually decreased the effectiveness from my observation. 

Modifications to existing srsue code
======
There are a few places that the original srsUE code has been changed. Firstly, accepting arguments from the toml file (changes made in ue.h, documenatation 
available there), `prach.h` (in phch and phy) (struct to pass attack parameters), `prach.cc`(calling our custom function), `worker_pool.cc`(setting config), `prach.c`
(main implementation).  

*See the NOTE on proc_ra_nr.cc which shows a very big limitation on srsRAN and why the test on srsRAN gNodeB might've been very effective (might not be as 
effective for blackbox RAN).*

Comments are provided in function implementation and function definitions in the files mentioned above wherever necessary.

Running the Code
=======
You can run it by building the code like you normally would (guide available in srsRAN docs too).

To make things easy, this project is dockerized, so running the following commands work and its very straightforward:
- `sudo docker build -t srsue:latest .`
- `sudo docker run --rm --privileged srsue:latest`

Debugging
===
If the effectiveness of the attack is poor, or if its not working at all, please check the logs on gNodeB and make sure that the PRACH preambles are being detected.
Sometimes, the preambles aren't detected at all (if gNodeB restarts or there's timing mismatch) and sometimes preambles stop being detected after a certain time.
So, monitor the logs from gNodeB (make sure that log level is set to debug to see the detected PRACH). Try `cat PATH_TO_GNODEB_FILE | grep "PRACH"` and see
if PRACH messages are being detected at very frequent occassions and also note the power at which the messages are being detected. If they're not, tweak parameters,
gain, etc.
