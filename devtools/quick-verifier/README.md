## Submit the builder

```
gcloud --project=chromeos-audio-qv builds submit builders/archlinux-builder --tag gcr.io/chromeos-audio-qv/archlinux-builder
```

### For kythe

```
gcloud --project=chromeos-audio-kythe builds submit builders/archlinux-builder --tag gcr.io/chromeos-audio-kythe/adhd-kzip-builder
```

## Submit the scheduler

```
gcloud --project=chromeos-audio-qv builds submit . --tag gcr.io/chromeos-audio-qv/scheduler
```

### Reset the VM

```
gcloud --project=chromeos-audio-qv compute instances reset instance-2
```

"Equivalent code" that used to create the VM instance:

```
gcloud compute instances create-with-container instance-2  \
    --project=chromeos-audio-qv \
    --zone=us-central1-a \
    --machine-type=e2-micro \
    --network-interface=network-tier=PREMIUM,subnet=default \
    --maintenance-policy=MIGRATE \
    --provisioning-model=STANDARD \
    --service-account=audio-qv@chromeos-audio-qv.iam.gserviceaccount.com \
    --scopes=https://www.googleapis.com/auth/cloud-platform \
    --image=projects/cos-cloud/global/images/cos-stable-101-17162-127-8 \
    --boot-disk-size=10GB \
    --boot-disk-type=pd-balanced \
    --boot-disk-device-name=instance-2 \
    --container-image=gcr.io/chromeos-audio-qv/scheduler \
    --container-restart-policy=never \
    --no-shielded-secure-boot \
    --shielded-vtpm \
    --shielded-integrity-monitoring \
    --labels=container-vm=cos-stable-101-17162-127-8
```
