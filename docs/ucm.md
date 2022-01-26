# UCM Usage Guide in CRAS

[TOC]

## Introduction

UCM lets users configure settings on the sound card for different use cases.
On ChromeOS, HiFi is the only use case we have for now. So we use HiFi.conf.
In the source tree, it is maintained in chromeos-bsp-{$BOARD} package.
For example, for kevin board HiFi.conf:

[~/trunk/src/overlays/overlay-kevin/chromeos-base/chromeos-bsp-kevin/files/audio-config/ucm-config/rk3399-gru-sound/HiFi.conf](https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/master/overlay-kevin/chromeos-base/chromeos-bsp-kevin/files/audio-config/ucm-config/rk3399-gru-sound/HiFi.conf?q=package:%5Echromeos_public$+rk3399-gru-sound/HiFi.conf&dr)

* It is in overlay repo chromeos-bsp-kevin package
* rk3399-gru-sound is the card name
* HiFi.conf is the config for HiFi use case.
* On device, it is at /usr/share/alsa/ucm/[Card]/HiFi.conf

The available controls can be queried by:

```
amixer -c0 contents
```


## Available card and devices provided by driver


```
aplay -l
```


or


```
arecord -l
```


Ref: aplay.c

```
printf(_("card %i: %s [%s], device %i: %s [%s]\n"),
		card, snd_ctl_card_info_get_id(info), snd_ctl_card_info_get_name(info),
		dev,
		snd_pcm_info_get_id(pcminfo),
		snd_pcm_info_get_name(pcminfo));
```

Notice:

*	For the cdev field, use card id to specify the card. Eg: cdev "hw:PCH"
*	For the Playback/CapturePCM field, use the card id and the device index to specify the device. Eg: PlaybackPCM "hw:PCH,0"

Another example:

```
localhost ~ # aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: sofcmlrt1011rt5 [sof-cml_rt1011_rt5682], device 0: Port1 (\*) []
  Subdevices: 1/1
  Subdevice #0: subdevice #0
PlaybackPCM: "sofcmlrt1011rt5,0"
```

To verify it works, try


```
 aplay -D hw:sofcmlrt1011rt5,0 -f dat /dev/zero
```


You can also `cat /proc/asound/cards` to find the card id.


## Audio Nodes in CRAS

A audio **Node** is the basic element of input or output audio path selection available to user on CRAS.
On a system, there can be multiple sound cards. On a sound card, there can be multiple devices.
On a device, CRAS can enumerate multiple nodes.

For example, in many old platforms, speaker and headphone use the same audio device,
and is switched by mixer controls like "Headphone Switch" or "Speaker Switch".
By controlling these mixers, CRAS can adjust output to headphone or speaker.
CRAS exposes two output nodes to Chrome, and they will be shown in Chrome audio selection UI with correct UI icons.

There are some audio nodes not available to user too.
For example, loopback node for casting, hotwording node for hotwording.

In legacy UCM, we let CRAS identify the existence of node through some logic and guessing.
For example, a mixer control named "Headphone Switch" indicates that there should be a headphone node.
A jack named "Headset Jack" indicates that there should be a headphone node.

In fully-specified UCM, we define the existence of node using SectionDevice.
All new devices should use fully-specified UCM.


## **Syntax**

Starting from kevin, we use fully-specified UCM.

The structure looks like this:

```
SectionVerb {}
SectionDevice."Speaker".0 {}
SectionDevice."Internal Mic".0 {}
SectionDevice."Headphone".0 {}
SectionDevice."Line Out".0 {}
SectionDevice."Mic".0 {}
SectionDevice."HDMI".0 {}
SectionDevice."Wake on Voice".0 {}
SectionModifier."Hotword Model ar_eg".0 {}
SectionModifier."Hotword Model cmn_cn".0 {}
...
```

There are three parts:



*   `SectionVerb`: Config about this card.
*   `SectionDevice`: Originally, it is about a "device" on this card.
	In CRAS, we have an additional layer, that is, "node".
	We borrow this syntax so SectionDevice actually configurates a "node" on a card.
*   `SectionModifier`: Define a modifier, that is, a pair of `EnableSequence` and `DisableSequence` of mixer controls that CRAS and apply them on demand. We use it for two purposes:
    *   To apply the different hotwording model for different language.
    *   To apply mixer controls to swap left and right channels (deprecated, replaced by software solution)

The structure of a `SectionVerb`:

```
SectionVerb {
Value {
		// Values to config this card.
	}
EnableSequence [
		// mixer control to be set when the card is initialized in CRAS.
	]
	DisableSequence [
		// mixer control to be set when the card is destroyed in CRAS.
	]
}
```

The structure of a `SectionDevice`:

```
SectionDevice."Speaker".0 { // The node name. Node name is one of the factors that determine node type.
	Value {
		// Values to config this node.
	}
	EnableSequence [
		// mixer control to be set when this node is selected in CRAS.
	]
	DisableSequence [
		// mixer control to be set when this node is unselected in CRAS.
	]
}
```

The structure of a `SectionModifier`:

```
SectionModifier."Hotword Model ar_eg".0 { // The modifier name.
	EnableSequence [
		// mixer control to be set when this modifier is enabled.
	]
	DisableSequence [
		// mixer control to be set when this modifier is disabled.
	]
}
```


## Values

Upstream alsa-lib has definitions of some common values [[ref](https://github.com/alsa-project/alsa-lib/blob/master/include/use-case.h#L256)].

All the available values are defined in [cras_alsa_ucm.c](https://chromium.googlesource.com/chromiumos/third_party/adhd/+/master/cras/src/server/cras_alsa_ucm.c).

If you see a value is defined in both upstream and CRAS, then you can use it unless there is discrepancy specified in this doc.

For values not defined in upstream, this section describes their meanings and usages.

(Last updated: 2020/06/03)


```
static const char jack_control_var[] = "JackControl";
static const char jack_dev_var[] = "JackDev";
static const char jack_switch_var[] = "JackSwitch";
static const char edid_var[] = "EDIDFile";
static const char cap_var[] = "CaptureControl";
static const char mic_positions[] = "MicPositions";
static const char override_type_name_var[] = "OverrideNodeType";
static const char dsp_name_var[] = "DspName";
static const char mixer_var[] = "MixerName";
static const char swap_mode_suffix[] = "Swap Mode";
static const char min_buffer_level_var[] = "MinBufferLevel";
static const char dma_period_var[] = "DmaPeriodMicrosecs";
static const char disable_software_volume[] = "DisableSoftwareVolume";
static const char playback_device_name_var[] = "PlaybackPCM";
static const char playback_device_rate_var[] = "PlaybackRate";
static const char capture_device_name_var[] = "CapturePCM";
static const char capture_device_rate_var[] = "CaptureRate";
static const char capture_channel_map_var[] = "CaptureChannelMap";
static const char coupled_mixers[] = "CoupledMixers";
static const char dependent_device_name_var[] = "DependentPCM";
static const char preempt_hotword_var[] = "PreemptHotword";
static const char echo_reference_dev_name_var[] = "EchoReferenceDev";
```

*   FullySpecifiedUCM: Used in SectionVerb. Set to "1" to enable fully specified UCM. As explained in

## Audio nodes CRAS

In fully-specified UCM, each SectionDevice will correspond to a node. In legacy UCM, CRAS has its logic of enumerating nodes.
*   Jack:
    *   JackSwitch: The switch event value for different type of audio device. E.g. on kevin. JackSwitch "6" for LineOut, and JackSwitch "2" for Headphone. The switch event is defined in kernel input_event_codes.h ([e.g.](http://elixir.free-electrons.com/linux/v4.5/source/include/uapi/linux/input-event-codes.h#L732))
*   Gain control:
    *   CaptureControl: Used in SectionDevice. The mixer control to control the gain of this section. E.g. "Mic". In fully-specified UCM, It is replaced by CaptureMixerElem.
    *   CoupledMixers: Used in SectionDevice. The mixer controls that should be changed together. E.g. "Left Master,Right Master". This means, mixer control for left speaker and mixer control for right speaker should be changed at the same time to the same value.
*   Capture Software node gain fine tune:
    *   IntrinsicSensitivity
        *   Used in SectionDevice for capture devices. It shows how sensitive CRAS thinks about the capture device when CRAS gets the audio samples after the whole audio stack (Ex: microphone module, DSP ...). The variable is then used as a reference for CRAS to set correct gain to the device.
        *   Generally, CRAS targets to apply gain so we get signals with -6 dBFS volume when the input is 94dB SPL. To achieve this we'll need to know how the intrinsic sensitivity of the board is to decide the correct gain.
        *   Notice: By Defining this variable, CRAS will apply **software gain** instead of hardware gain.
        *   E.g: "-2600" means the input device captures -26 dBFS under 94 dBSPL 1k sine wave. CRAS will calculate that we need an extra 20 dB gain for
        *   Detailed guide about how to measure the value:
            *   (Partners only) https://chromeos.google.com/partner/dlm/docs/p-hardware-specs/audiotuning.html
*   Software volume/gain:
    *   DisableSoftwareVolume: Used in SectionVerb. Set to "1" to disable software volume on this card. When the volume range provided by hardware control on this card is limited, CRAS automatically decides to use software volume. This flag changes that behavior.
    *   By default, we rely on the existence of IntrinsicSensitivity to determine if the software gain instead of hardware gain is applied.
*   Mic:
    *   CaptureChannelMap:  Used in SectionDevice. The selected channel map for multiple-channel mics.  \
E.g. "2 3 0 1 -1 -1 -1 -1 -1 -1 -1" means, FL takes channel 2, FR takes channel 3, RL takes channel 0, RL takes channel 1. \
E.g.  "0 1 -1 -1 -1 -1 -1 -1 -1 -1 -1" means, FL takes channel 0, FR takes channel 1. The channels in the array are defined in [cras_audio_format.c](https://chromium.googlesource.com/chromiumos/third_party/adhd/+/master/cras/src/common/cras_audio_format.c#19). The meaning of the channel name follows [ALSA PCM channel mapping API](https://www.kernel.org/doc/html/v4.10/sound/designs/channel-mapping-api.html).
*   DSP:
    *   DspName: Used in SectionDevice. DSP name specified in dsp.ini. E.g. "speaker_eq" or "dmic_eq".
*   Scheduling:
    *   DmaPeriodMicrosecs: Set DMA period in ms.
    *   EnableHtimestamp: Used in SectionVerb. Use [htimestamp](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#ga3946abd34178b3de60fd5329b71c189b) and enable PCM timestamp mode `SND_PCM_TSTAMP_ENABLE` in ALSA API for better buffer level precision in timing.
*   Hotword:
    *   PreemptHotword: "1" to enable. This is usually used on Internal Mic node. This means, when this node is in use, disable hotwording device. When this is not in use, resume hotwording device.
*   Dependent PCM
    *   There's a case that two ALSA PCMs cannot be open at the same time due to hardware restriction. Add DependentPCM flag in UCM so that we can specify one device as a child node of the other. Nodes on the same CRAS iodev are exclusive, guaranteeing the two PCMs will not be open simultaneously.
*   Echo reference
    *   Used to specify one ALSA dev as the echo reference to another playback dev. A typical example is to specify one dev as echo reference of main speaker.
*   Legacy: (Do not use these on new boards)
    *   Swap Mode: Define a SectionModifier. Still used on some boards to define a set of mixer controls to swap left and right channels.
    *   MainVolumeNames: Used in SectionVerb. In legacy UCM, specify the main volume controls which controls all nodes on this card.
    *   OverrideNodeType: Used in SectionDevice. Override the node type: e.g. "Internal Speaker". This is used in legacyNon-fully-specified UCM to override node type that does not follow the logic.


## Apply UCM and Debug

```
alsaucm -c <card name here> set _verb HiFi
```

## Examples

Look for HiFi.conf on [codesearch](https://source.chromium.org/search?q=file:HiFi.conf&ss=chromiumos)
