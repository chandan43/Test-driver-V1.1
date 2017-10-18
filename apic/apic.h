#define APIC_MAGIC    'A'
#define APIC_GETID  _IOW(APIC_MAGIC,1,int *)
#define APIC_GETIRQ _IOW(APIC_MAGIC,2,int *)

#define DEVICE_NAME "myapic"
#define IOAPIC_BASE 0xFEC00000
