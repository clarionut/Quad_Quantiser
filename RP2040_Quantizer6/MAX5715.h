// MAX5715 Commands

// Set reference and power it if at least one DAC is on
// External
#define REFEXT 0x70
// Internal 2.5V
#define REFINT25 0x71
// Internal 2.0V
#define REFINT20 0x72
// Internal 4.1V
#define REFINT41 0x73

// Set reference and power it always (second parameter ignored)
// External
#define REFEXTA 0x74
// Internal 2.5V
#define REFI25A 0x75
// Internal 2.048V
#define REFI20A 0x76
// Internal 4.096V
#define REFI41A 0x77

// Set code register n (followed by 12-bit data, final 4 bits ignored)
#define CODEA 0x00
#define CODEB 0x01
#define CODEC 0x02
#define CODED 0x03

// Load DAC n from its code register
#define LOADA 0x10
#define LOADB 0x11
#define LOADC 0x12
#define LOADD 0x13

// Set code register n and load all (followed by 12-bit data, final 4 bits ignored)
#define CODEA_LOADALL 0x20
#define CODEB_LOADALL 0x21
#define CODEC_LOADALL 0x22
#define CODED_LOADALL 0x23

// Set code register n and load it to DAC n
#define CODEA_LOADA 0x30
#define CODEB_LOADB 0x31
#define CODEC_LOADC 0x32
#define CODED_LOADD 0x33

// Powerdown mode (followed by data to specify affected DACs - 0b0000abcd00000000)
#define PWR_NORM 0x40
#define PWR_1KPD 0x41
#define PWR_100K 0x42
#define PWR_HI_Z 0x43

// Set all code registers (followed by 12-bit data, final 4 bits ignored)
#define CODE_ALL 0x80

// Load all DACs from their code registers
#define LOAD_ALL 0x81

// load all code regs and all dac regs (followed by 12-bit data, final 4 bits ignored)
#define CODEALL_LOADALL 0x82
