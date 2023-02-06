# USB UCM Contribution Guide
This is a guide for partners contributing USB UCM files for CRAS. This USB UCM config may be required when you encounter the following situation.
 - Your audio device cannot change volume correctly.
 - Your audio devices have some issues when using software volume. To identify whether chrome os is using software volume, you can check the log in /var/log/message on your chromebook. You should see something like
    ```
    2022-10-04T10:54:53.790175Z WARNING cras_server[2384]: Razer Kraken 7.1 Chroma: USB Audio:1,0: Headset output number_of_volume_steps [0] is abnormally small.Fallback to software volume and set number_of_volume_steps to 25
    ```
 - You suspect CRAS is outputting an error log like the following, and you suspect that is the root cause of the audio issue.
    ```
    2023-01-31T09:24:49.221278Z ERR cras_server[2285]: [alsa-lib] uc_mgr_config_load() -> could not open configuration file /usr/share/alsa/ucm2/Razer Kraken 7.1 Chroma/Razer Kraken 7.1 Chroma.conf
    2023-01-31T09:24:49.221303Z ERR cras_server[2285]: [alsa-lib] load_master_config() -> error: could not parse configuration for card Razer Kraken 7.1 Chroma
    2023-01-31T09:24:49.221329Z ERR cras_server[2285]: [alsa-lib] snd_use_case_mgr_open() -> error: failed to import Razer Kraken 7.1 Chroma use case configuration -2
    ```

## Prerequisites
 - [Able to contribute chrome os patch](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/contributing.md)
 - [Setup chrome os development environment](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md)
 - USB flash disk (8GB or bigger)

## Setup
 - Installing ChromiumOS on your Device with latest version.
 - Remove rootfs.
    ```
    // on your chromebook
    $ /usr/libexec/debugd/helpers/dev_features_rootfs_verification
    $ reboot
    ```
 - Connect your chromebook to your LAN, and get the chromebook ip address.
 - Connect your USB device to your chromebook.

## Create and test UCM files
 - Open debug server
```
$ cd ~/chromiumos
$ cros_sdk
$ cd chromiumos/third_party/adhd/ucm-config/
$ ./usb_deivce_ucm_server.py
```
 - Open your browser and enter localhost:9090
 - In the IP placeholder, please enter your chromebook ip address and click connect. And wait until the OK alert from the browser.
 - Select mixer controls with the dropdown menu in the playback and capture section. If you don’t choose or dropdown is empty. It is possible that some devices don’t have that functionality. For example, USB microphones don’t have playback mixer controls.
 - Select whether you want to disable software volume for your audio device.
 - Click deploy ucm to chromebook after your setting is done, then wait browser alert OK.
 - Test with your machine and check whether you can resolve your issue.
 - If above step fails, then try another combination with playback mixer controls, capture mixer controls and disable software volume option. If all settings cannot resolve your issue, then you can stop here. That means adding UCM for your device is not helping anything with the issue.

After you are done with the above steps. The system will automatically generate UCM files under the correct path.
You should see your ucm files have been created under `~/chromiumos/src/third_party/adhd/ucm-config/for_all_boards/`