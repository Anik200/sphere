#include "search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#endif

// Safe URL encoder
static void url_encode(const char *src, char *dst, size_t dst_len) {
    static const char *hex = "0123456789ABCDEF";
    size_t d = 0;
    for (size_t s = 0; src[s] && d + 4 < dst_len; s++) {
        unsigned char c = (unsigned char)src[s];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[d++] = (char)c;
        } else if (c == ' ') {
            dst[d++] = '+';
        } else {
            dst[d++] = '%';
            dst[d++] = hex[c >> 4];
            dst[d++] = hex[c & 0x0F];
        }
    }
    dst[d] = '\0';
}

// Simple JSON string value extractor
static int extract_json_field(const char *json, const char *key, char *out_val, size_t max_len) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        // Try without opening quote for nested objects or numbers
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        pos = strstr(json, pattern);
    }
    if (!pos) return 0;

    pos += strlen(pattern);
    size_t o = 0;
    while (*pos && o + 1 < max_len) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            if (*pos == 'n') out_val[o++] = ' ';
            else if (*pos == 't') out_val[o++] = ' ';
            else if (*pos == '"') out_val[o++] = '"';
            else if (*pos == '\\') out_val[o++] = '\\';
            else if (*pos == 'u' && isxdigit((unsigned char)*(pos+1)) && isxdigit((unsigned char)*(pos+2)) &&
                     isxdigit((unsigned char)*(pos+3)) && isxdigit((unsigned char)*(pos+4))) {
                char hexbuf[5] = { *(pos+1), *(pos+2), *(pos+3), *(pos+4), '\0' };
                unsigned long cp = strtoul(hexbuf, NULL, 16);
                if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) {
                    out_val[o++] = '-';
                } else if (cp == 0x2018 || cp == 0x2019) {
                    out_val[o++] = '\'';
                } else if (cp == 0x201C || cp == 0x201D) {
                    out_val[o++] = '"';
                } else if (cp == 0x00A0) {
                    out_val[o++] = ' ';
                } else if (cp < 128) {
                    out_val[o++] = (char)cp;
                }
                pos += 4;
            } else {
                out_val[o++] = *pos;
            }
        } else if (*pos == '"') {
            break;
        } else {
            out_val[o++] = *pos;
        }
        pos++;
    }
    out_val[o] = '\0';
    return o > 0;
}

static int extract_opensearch_title(const char *json, char *out_title, size_t max_len) {
    if (!json || !out_title || max_len == 0) return 0;
    out_title[0] = '\0';

    // OpenSearch format: ["query", ["Title 1", "Title 2"], ...]
    const char *p = strchr(json, '[');
    if (!p) return 0;
    p++;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == ']') return 0; // Empty result array

    if (*p == '"') {
        p++;
        size_t o = 0;
        while (*p && *p != '"' && o + 1 < max_len) {
            if (*p == '\\' && *(p + 1)) {
                p++;
                out_title[o++] = *p;
            } else {
                out_title[o++] = *p;
            }
            p++;
        }
        out_title[o] = '\0';
        return o > 0;
    }
    return 0;
}

static void clean_image_caption(const char *url, char *caption, size_t max_len) {
    const char *last_slash = strrchr(url, '/');
    if (!last_slash) last_slash = url;
    else last_slash++;

    // Skip leading dimension prefixes like "60px-", "330px-", etc.
    const char *p = last_slash;
    while (isdigit((unsigned char)*p)) p++;
    if (strncmp(p, "px-", 3) == 0) {
        last_slash = p + 3;
    }

    size_t o = 0;
    for (size_t i = 0; last_slash[i] && last_slash[i] != '?' && o + 1 < max_len; i++) {
        if (last_slash[i] == '_' || last_slash[i] == '-') {
            caption[o++] = ' ';
        } else if (last_slash[i] == '%' && isxdigit(last_slash[i+1]) && isxdigit(last_slash[i+2])) {
            caption[o++] = ' ';
            i += 2;
        } else if (last_slash[i] == '.') {
            break; // Stop at extension (.jpg, .png)
        } else {
            caption[o++] = last_slash[i];
        }
    }
    caption[o] = '\0';
}

static void extract_subject(const char *raw, char *clean, size_t max_len) {
    if (!raw || !clean || max_len == 0) return;
    clean[0] = '\0';

    // 1. Skip UTF-8 BOM if present
    const unsigned char *src = (const unsigned char *)raw;
    if (src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        src += 3;
    }
    // 2. Skip leading whitespace and quotes
    while (*src && (isspace(*src) || *src == '"' || *src == '\'' || *src == '`')) {
        src++;
    }

    char lower[512];
    size_t i = 0;
    for (; src[i] && i + 1 < sizeof(lower); i++) {
        lower[i] = (char)tolower((unsigned char)src[i]);
    }
    lower[i] = '\0';

    const char *start = lower;
    const char *prefixes[] = {
        "can you teach me about ", "can you teach me how to ", "can you teach me ",
        "teach me about ", "teach me how to ", "teach me ",
        "can you learn me ", "can you help me learn about ", "can you help me learn ",
        "can you help me with ", "can you help me ",
        "help me learn about ", "help me learn ", "help me with ",
        "i want to learn about ", "i want to learn how to ", "i want to learn ",
        "how do i learn about ", "how do i learn ", "how can i learn about ", "how can i learn ",
        "can you explain to me ", "can you explain about ", "can you explain ",
        "explain to me about ", "explain to me ", "explain ",
        "tell me how to ", "tell me all about ", "tell me about a ", "tell me about an ", "tell me about the ", "tell me about ",
        "tell me what a ", "tell me what is ", "tell me why the ", "tell me why ",
        "can you tell me about ", "can you tell me why ", "can you tell me ", "can you show me how to ", "can you show me ",
        "can you give me information about ", "can you give me information on ", "can you give me ",
        "give me information about ", "give me information on ",
        "what can you tell me about ",
        "can we talk about ", "can we learn about ", "can we discuss ",
        "if you can teach me ", "if it can teach me ", "if you can tell me ",
        "why do the ", "why do a ", "why do an ", "why do ",
        "why does the ", "why does a ", "why does an ", "why does ",
        "why is the ", "why is a ", "why is an ", "why is ",
        "why are the ", "why are a ", "why are an ", "why are ",
        "why did the ", "why did a ", "why did ",
        "why would the ", "why would ",
        "why ",
        "how do the ", "how do a ", "how do an ", "how do ",
        "how does the ", "how does a ", "how does an ", "how does ",
        "how can the ", "how can a ", "how can an ", "how can ",
        "how is the ", "how is a ", "how is an ", "how is ",
        "how are the ", "how are a ", "how are an ", "how are ",
        "how tall is the ", "how tall is ", "how old is the ", "how old is ",
        "how big is the ", "how big is ", "how high is the ", "how high is ",
        "how to ", "how ",
        "what is the ", "what is a ", "what is an ", "what is ",
        "what was the ", "what was a ", "what was an ", "what was ",
        "what does a ", "what does an ", "what does the ", "what does ",
        "what do a ", "what do an ", "what do the ", "what do ",
        "what are the ", "what are a ", "what are ",
        "who is the ", "who is a ", "who is an ", "who is ",
        "who was the ", "who was a ", "who was an ", "who was ",
        "who were the ", "who were ",
        "who created ", "who invented ", "who discovered ", "who made ",
        "describe a ", "describe an ", "describe the ", "describe ",
        "where is the ", "where is a ", "where is an ", "where is ", "where are ",
        "when was the ", "when was ", "when did ", "when is the ", "when is ",
        "search for a ", "search for an ", "search for ",
        "picture of a ", "picture of an ", "picture of ",
        "image of a ", "image of an ", "image of ",
        "show me a ", "show me an ", "show me "
    };
    for (size_t p = 0; p < sizeof(prefixes)/sizeof(prefixes[0]); p++) {
        size_t plen = strlen(prefixes[p]);
        if (strncmp(start, prefixes[p], plen) == 0) {
            start += plen;
            break;
        }
    }

    size_t d = 0;
    while (*start && d + 1 < max_len) {
        clean[d++] = *start++;
    }
    clean[d] = '\0';

    while (d > 0 && (clean[d-1] == '?' || clean[d-1] == '.' || clean[d-1] == '!' || clean[d-1] == ' ' || clean[d-1] == '"' || clean[d-1] == '\'')) {
        clean[--d] = '\0';
    }

    const char *suffixes[] = {
        " for interactive chats", " in interactive chats", " for interactive chat", " interactively",
        " for me", " please",
        " look like", " looks like", " looking like", " appear like", " appear", " seem like",
        " mean", " means", " meaning", " work", " works"
    };
    for (size_t s = 0; s < sizeof(suffixes)/sizeof(suffixes[0]); s++) {
        size_t slen = strlen(suffixes[s]);
        if (d >= slen && strcmp(clean + d - slen, suffixes[s]) == 0) {
            d -= slen;
            clean[d] = '\0';
            break;
        }
    }

    while (d > 0 && (clean[d-1] == ' ' || clean[d-1] == '?' || clean[d-1] == '.' || clean[d-1] == '!')) {
        clean[--d] = '\0';
    }

    if (d == 0) {
        strncpy(clean, raw, max_len - 1);
        clean[max_len - 1] = '\0';
    }
}

// ---------------------------------------------------------
// Comprehensive Native Offline Knowledge Base (0% Internet)
// ---------------------------------------------------------
typedef struct {
    const char *keywords;
    const char *title;
    const char *extract;
} OfflineArticle;

static const OfflineArticle OFFLINE_KB[] = {
    // 1. Computer Hardware, Peripherals & Electronics
    {
        "computer mouse,pc mouse,optical mouse,trackball,gaming mouse,usb mouse",
        "Computer mouse",
        "A computer mouse is a hand-held electronic pointing device that detects two-dimensional motion relative to a surface. "
        "This motion is translated into the motion of a pointer on a display, allowing smooth control of the graphical user interface. "
        "Modern computer mice feature optical or laser movement detection, left and right click buttons, and tactile scroll wheels."
    },
    {
        "mouse,mice,house mouse,field mouse,rodent,rodents",
        "Mouse",
        "A mouse is a small mammal and rodent characteristically known to have a pointed snout, small rounded ears, a body-length scaly tail, and a high breeding rate. "
        "The best known mouse species is the common house mouse. Mice are adaptable nocturnal animals that compensate for poor eyesight with acute hearing and smell."
    },
    {
        "keyboard,computer keyboard,mechanical keyboard",
        "Computer keyboard",
        "A computer keyboard is a peripheral input device modeled after the typewriter keyboard which uses an arrangement of buttons or keys to act as mechanical levers or electronic switches. "
        "It provides the primary alphanumeric data entry interface for desktop and laptop computers."
    },
    {
        "monitor,computer monitor,computer screen,display screen,oled monitor",
        "Computer monitor",
        "A computer monitor is an output device that displays information in pictorial or text form. "
        "Modern monitors use thin-film transistor liquid-crystal displays with LED backlighting or OLED panels, offering high refresh rates and ultra-high resolution."
    },
    {
        "laptop,notebook,macbook,chromebook",
        "Laptop computer",
        "A laptop computer is a small, portable personal computer with a clamshell form factor, typically having a thin display screen mounted on the inside of the upper lid and an alphanumeric keyboard on the inside of the lower lid."
    },
    {
        "smartphone,phone,cell phone,mobile phone,iphone,android",
        "Smartphone",
        "A smartphone is a portable computer device that combines mobile telephone functions and personal computing capabilities into one unit. "
        "They feature capacitive touchscreens, integrated high-resolution cameras, GPS navigation, and cellular mobile broadband connectivity."
    },
    {
        "cpu,processor,central processing unit,microprocessor",
        "Central processing unit",
        "The central processing unit is the primary electronic circuitry within a computer that executes software instructions. "
        "It performs basic arithmetic, logic, controlling, and input/output operations specified by computer programs."
    },
    {
        "gpu,graphics card,video card,nvidia,radeon",
        "Graphics processing unit",
        "A graphics processing unit is a specialized electronic circuit designed to rapidly manipulate and alter memory to accelerate the creation of images in a frame buffer for display devices, as well as powering artificial intelligence models."
    },
    {
        "ram,memory,random access memory",
        "Random-access memory",
        "Random-access memory is a form of electronic computer data storage that stores machine code and data currently in use. "
        "It permits data to be accessed randomly in constant time regardless of where it is physically located."
    },
    {
        "ssd,solid state drive,hard drive,hard disk,hdd,storage",
        "Solid-state drive",
        "A solid-state drive is a solid-state data storage device that uses integrated circuit assemblies to store data persistently, typically using NAND flash memory. "
        "SSDs offer significantly higher throughput and lower read-write latency than mechanical magnetic drives."
    },
    {
        "headphone,headphones,headset,earphone,earphones,airpods",
        "Headphones",
        "Headphones are a pair of small loudspeaker drivers worn on or around the head over a user's ears. "
        "They convert an electrical audio signal into corresponding acoustic sound waves directly into the listener's ear canal."
    },
    {
        "microphone,mic,sound recording",
        "Microphone",
        "A microphone is a transducer that converts sound waves in air into an electrical audio signal. "
        "They are widely employed in voice recognition systems, studio recording, telecommunications, and acoustic measurement."
    },
    {
        "internet,web,world wide web,wifi,wi-fi",
        "Internet",
        "The Internet is a global system of interconnected computer networks that uses the Internet protocol suite to link billions of devices worldwide. "
        "It carries a vast range of information resources and services, including electronic mail, file transfer, and the World Wide Web."
    },
    {
        "artificial intelligence,ai,machine learning,deep learning",
        "Artificial intelligence",
        "Artificial intelligence is the field of computer science dedicated to creating computational systems capable of performing tasks that typically require human intelligence, such as visual perception, speech recognition, decision-making, and language translation."
    },
    {
        "python,python programming",
        "Python (programming language)",
        "Python is a high-level, general-purpose programming language designed with an emphasis on code readability. "
        "It supports multiple programming paradigms and has become one of the most popular languages for artificial intelligence, data science, and software automation."
    },
    {
        "c,c language,c programming",
        "C (programming language)",
        "The C programming language is an imperative procedural computer programming language developed in 1972 by Dennis Ritchie at Bell Labs. "
        "C provides low-level hardware memory access and high-efficiency compilation, powering modern operating system kernels, compilers, and embedded devices."
    },
    {
        "operating system,os,windows,linux,macos",
        "Operating system",
        "An operating system is system software that manages computer hardware, software resources, and provides common services for computer applications. "
        "Prominent operating systems include Microsoft Windows, Linux, and Apple macOS."
    },
    {
        "programming language,coding language,programming languages",
        "Programming language",
        "A programming language is a formal system of notation designed to communicate instructions to a computer. "
        "Different languages are optimized for distinct engineering disciplines, from high-performance systems and game engines (C, C++, Rust), to artificial intelligence (Python), and web engineering (JavaScript, TypeScript)."
    },
    {
        "best programming language,choose programming language,which programming language",
        "Choosing a programming language",
        "Choosing the ideal programming language depends on your project domain and performance requirements: Python dominates artificial intelligence and data science; TypeScript and JavaScript dominate web platforms; and C, C++, and Rust dominate high-performance game engines and systems software."
    },
    {
        "shirt,dress shirt,clothing,flat shirt,checkered shirt,cheque shirt,plaid shirt",
        "Shirt and menswear styling",
        "A shirt is an upper-body tailored garment characterized by collars, sleeves, and a buttoned front placket. "
        "In modern style, flat solid-color shirts deliver sleek, timeless formality for business and suits, while cheque and checkered shirts introduce textured depth and casual charm for smart-casual wear."
    },
    {
        "cheque vs flat,checkered vs flat,shirt choice",
        "Cheque vs flat shirts",
        "Choosing between a cheque and a flat shirt depends on formality and visual balance: flat shirts offer clean, versatile minimalism suited for business suits, job interviews, and formal dinners; cheque shirts bring visual interest, personality, and smart-casual charisma."
    },

    // 2. Animals & Biology
    {
        "cat,kitten,feline,domestic cat",
        "Cat",
        "The cat is a domestic species of small carnivorous mammal. It is the only domesticated species in the family Felidae, "
        "characterized by a flexible body, quick reflexes, sharp retractable claws, and teeth adapted to hunting small prey."
    },
    {
        "dog,puppy,canine,domestic dog",
        "Dog",
        "The dog is a domesticated descendant of the wolf. Highly attuned to human behavior, dogs are celebrated as loyal companions, "
        "working animals, and hunting partners across virtually every culture worldwide."
    },
    {
        "tiger,panthera tigris",
        "Tiger",
        "The tiger is the largest living cat species and a member of the genus Panthera. "
        "It is instantly recognizable for its dark vertical stripes on reddish-orange fur with a lighter underside, functioning as an apex predatory mammal in Asian forests."
    },
    {
        "lion,panthera leo",
        "Lion",
        "The lion is a large cat of the genus Panthera native to sub-Saharan Africa and western India. "
        "It is a social apex predator living in family prides, with adult males displaying majestic protective manes around their heads."
    },
    {
        "elephant",
        "Elephant",
        "Elephants are the largest living land animals on Earth. They possess massive bodies, large ear flaps used for thermal regulation, "
        "curved ivory tusks, and a prehensile muscular trunk capable of delicate dexterity."
    },
    {
        "wolf,gray wolf,canis lupus",
        "Wolf",
        "The wolf is a large canine native to Eurasia and North America. It is renowned for its pack social structure, "
        "sophisticated communication through howling, and exceptional endurance in traversing wilderness territory."
    },
    {
        "bear,grizzly,polar bear",
        "Bear",
        "Bears are large carnivoran mammals of the family Ursidae. They possess stocky legs, long snouts, heavy claws, and thick shaggy coats, "
        "occupying diverse ecological niches ranging from the polar ice caps to temperate forests."
    },
    {
        "dolphin,porpoise",
        "Dolphin",
        "Dolphins are marine mammals belonging to the order Cetacea. They are famous for their playful social nature, "
        "sleek hydrodynamic bodies, advanced acoustic echolocation, and high cognitive problem-solving abilities."
    },
    {
        "shark,great white shark",
        "Shark",
        "Sharks are elasmobranch cartilaginous fish that have inhabited the oceans for over four hundred million years. "
        "They possess streamlined predatory bodies, multiple rows of replaceable sharp teeth, and keen sensory perception of electrical fields."
    },
    {
        "bird,eagle,hawk,falcon",
        "Bird",
        "Birds are feathered, winged, warm-blooded vertebrate animals characterized by toothless beaked jaws, "
        "hollow lightweight skeletons, and high metabolic rates that enable powered flight."
    },
    {
        "snake,reptile,serpent",
        "Snake",
        "Snakes are elongated, legless carnivorous reptiles of the suborder Serpentes. "
        "They navigate terrain by muscular body waves and swallow prey whole using highly flexible and kinetic skulls."
    },
    {
        "human,homo sapiens,person,people",
        "Human",
        "Humans are bipedal primate mammals characterized by advanced cognitive faculties, abstract reasoning, symbolic speech, "
        "and the ability to engineer complex technologies and global societies."
    },

    // 3. Nature, Cosmos & Earth
    {
        "sunflower,helianthus",
        "Sunflower",
        "The sunflower is a tall annual plant native to the Americas, famous for its large vibrant yellow flower head. "
        "Its central seeds grow in a spiral pattern adhering strictly to mathematical Fibonacci ratios."
    },
    {
        "tree,forest,oak tree,pine tree",
        "Tree",
        "A tree is a perennial plant with an elongated wooden trunk that supports branching limbs and foliage. "
        "Trees anchor soil, produce atmospheric oxygen through photosynthesis, and form the cornerstone of terrestrial ecosystems."
    },
    {
        "sun,the sun,solar",
        "Sun",
        "The Sun is the star at the center of the Solar System. It is an incandescent sphere of superheated hydrogen and helium plasma, "
        "powering life on Earth through continuous nuclear fusion at its core."
    },
    {
        "moon,the moon,lunar",
        "Moon",
        "The Moon is Earth's only natural satellite. It is the fifth largest moon in the Solar System, with a cratered dusty surface "
        "and gravitational influence that drives Earth's ocean tides."
    },
    {
        "earth,planet earth,the earth",
        "Earth",
        "Earth is the third planet from the Sun and the only astronomical body verified to harbor living organisms. "
        "Over seventy percent of its surface is covered by liquid water oceans, framed by a protective atmosphere."
    },
    {
        "mars,red planet",
        "Mars",
        "Mars is the fourth planet from the Sun and the second smallest in the Solar System. "
        "Its rusty reddish color is caused by ubiquitous iron oxide across its rocky desert surface, marked by ancient dried river valleys."
    },
    {
        "black hole,singularity,event horizon",
        "Black hole",
        "A black hole is a region of spacetime where gravitational acceleration is so intense that nothing, not even electromagnetic radiation such as light, can escape. "
        "Around it, superheated cosmic dust forms a glowing accretion disk warped by intense gravitational lensing."
    },
    {
        "galaxy,milky way,andromeda",
        "Galaxy",
        "A galaxy is a massive gravitationally bound system of stars, stellar remnants, interstellar gas, cosmic dust, and dark matter. "
        "The Milky Way contains between one hundred and four hundred billion stars arranged in sweeping spiral arms."
    },
    {
        "ocean,sea,pacific ocean,atlantic ocean",
        "Ocean",
        "An ocean is a vast body of saline water that blankets more than seventy percent of Earth's surface. "
        "Oceans drive climate cycles, house extraordinary marine biodiversity, and regulate global thermal equilibrium."
    },
    {
        "mountain,mount everest,himalayas",
        "Mountain",
        "A mountain is a large natural elevation of the Earth's surface rising abruptly from the surrounding level, "
        "formed over millions of years by tectonic collisions and volcanic activity."
    },

    // 4. Vehicles & Landmarks
    {
        "car,automobile,vehicle,motorcar",
        "Car",
        "A car is a wheeled motor vehicle used for transporting passengers on roadways. "
        "Modern automobiles are powered by internal combustion engines or high-capacity electric battery packs within sculpted aerodynamic chassis."
    },
    {
        "airplane,plane,aeroplane,aircraft,jet",
        "Airplane",
        "An airplane is a powered fixed-wing aircraft propelled forward by jet engine or propeller thrust. "
        "Airplanes achieve sustained flight through aerodynamic wing lift, enabling intercontinental travel across the upper atmosphere."
    },
    {
        "rocket,spacecraft,spaceship",
        "Rocket",
        "A rocket is a spacecraft or high-speed vehicle propelled by rocket engine thrust through the expulsion of reaction mass. "
        "Rockets are the sole machines capable of escaping planetary gravity to navigate outer space."
    },
    {
        "bicycle,bike,cycle",
        "Bicycle",
        "A bicycle is a pedal-driven human-powered vehicle with two wheels attached to a rigid frame. "
        "It provides clean, highly energy-efficient personal transportation globally."
    },
    {
        "train,railway,locomotive",
        "Train",
        "A train is a connected series of rail cars guided by tracks to transport passengers or heavy freight across land distances, "
        "propelled by powerful electric or diesel-electric locomotives."
    },
    {
        "eiffel tower,paris tower",
        "Eiffel Tower",
        "The Eiffel Tower is a wrought-iron lattice tower located on the Champ de Mars in Paris, France. "
        "Constructed in 1889 as the centerpiece for the World's Fair, it stands 330 meters tall as a global symbol of architectural engineering."
    },
    {
        "pyramid,pyramids of giza,egyptian pyramids",
        "Egyptian pyramids",
        "The Egyptian pyramids are monumental masonry structures built in ancient Egypt as royal tombs for pharaohs. "
        "The Great Pyramid of Giza is the oldest of the Seven Wonders of the Ancient World and the only one to remain largely intact."
    },
    {
        "book,novel,literature",
        "Book",
        "A book is a medium for recording information in the form of writing or images, traditionally composed of bound paper sheets "
        "protected by a binding cover."
    },
    {
        "clock,watch,timepiece,wrist watch",
        "Clock",
        "A clock is an instrument designed to measure, verify, and indicate time. "
        "Modern timekeeping relies on quartz crystal oscillation or atomic resonance frequencies to maintain microsecond chronological precision."
    },
    {
        "calculus,differential calculus,integral calculus,infinitesimal calculus",
        "Calculus",
        "Calculus is the mathematical study of continuous change, in the same way that geometry is the study of shape, and algebra is the study of generalizations of arithmetic operations. "
        "It has two major branches: differential calculus concerning instantaneous rates of change and slopes of curves, and integral calculus concerning accumulation of quantities and areas under curves. "
        "These two branches are related to each other by the fundamental theorem of calculus."
    },
    {
        "mathematics,math,maths",
        "Mathematics",
        "Mathematics is an area of knowledge that includes the topics of numbers, formulas and related structures, shapes and spaces, and quantities and their changes. "
        "Most mathematical activity involves using pure reasoning to discover or prove properties of abstract objects."
    },
    {
        "physics,theoretical physics",
        "Physics",
        "Physics is the natural science of matter, its fundamental constituents, its motion and behavior through space and time, and the related entities of energy and force. "
        "Physics is one of the most fundamental scientific disciplines, with its main goal being to understand how the universe behaves."
    },
    // 5. Scientific, Biological & Natural Phenomena
    {
        "why spiders hang upside down,why do spiders hang upside down,spiders hang upside down,spider hang upside down,hanging upside down,spider upside down,spiders hanging upside down,spider,spiders,arachnid,spider web",
        "Why spiders hang upside down",
        "Spiders, particularly orb-web weavers, hang upside down in their webs for several crucial biological and mechanical reasons: First, gravity gives them an instant downward speed burst to drop rapidly onto prey trapped in the web below. Second, spider legs operate hydraulically using hemolymph fluid pressure rather than extensor muscles; hanging upside down by their tarsal claws lets their leg muscles completely relax with almost zero energy consumption. It also makes it much easier to molt out of their old exoskeleton and helps them blend in beneath foliage to evade predatory birds."
    },
    {
        "why is the sky blue,sky blue,why sky is blue,rayleigh scattering,blue sky,color of the sky",
        "Why the sky is blue",
        "The sky appears blue because of Rayleigh scattering in Earth's atmosphere. Sunlight contains all colors of the rainbow combined. When sunlight penetrates Earth's atmosphere, gases and microscopic air molecules scatter the light in every direction. Short-wavelength blue light scatters much more vigorously than longer red or yellow wavelengths, filling the entire sky with diffuse blue illumination."
    },
    {
        "how airplanes fly,how do airplanes fly,how planes fly,how do planes fly,aerodynamic lift,airfoil,bernoulli's principle",
        "How airplanes fly",
        "Airplanes fly by generating aerodynamic lift through their wings. As engines propel the aircraft forward, air flows over and under the wing's curved airfoil profile. Air travels faster over the curved upper surface, creating lower air pressure above the wing than underneath it. This pressure differential generates an upward aerodynamic lift force that overcomes gravity and sustains flight."
    },
    {
        "why do cats purr,why cats purr,cat purring,purring,purr",
        "Why cats purr",
        "Cats purr by rapidly vibrating their vocal cords and laryngeal muscles between 25 and 150 times per second. While purring commonly signals contentment when relaxing with their owners, cats also purr when stressed, frightened, or injured. Scientists have discovered that these specific acoustic frequencies stimulate bone regeneration, ease breathing, repair muscle tissue, and act as a natural pain reliever."
    },
    {
        "why is the ocean salty,why ocean is salty,salty ocean,ocean salt,why sea is salty",
        "Why the ocean is salty",
        "The ocean is salty primarily due to the weathering of rocks on land over billions of years. Rain contains dissolved carbon dioxide that makes it slightly acidic, slowly eroding minerals and salts from continental rocks. Rivers and streams wash these dissolved ions, mainly sodium and chloride, into the ocean. When seawater evaporates to form rainclouds, the salt minerals are left behind, gradually concentrating over eons."
    },
    {
        "why do leaves change color,why leaves change color,autumn leaves,fall foliage,chlorophyll breakdown,leaves turning yellow",
        "Why leaves change color in autumn",
        "Leaves change color in autumn because trees prepare for winter by shutting down photosynthesis. During spring and summer, abundant green chlorophyll masks all other pigments. As daylight shortens and temperatures drop, chlorophyll production stops and existing chlorophyll decomposes, unveiling vibrant yellow and orange carotenoids, as well as brilliant red anthocyanin pigments."
    },
    {
        "why do we dream,why we dream,dreaming,dreams,rem sleep,purpose of dreams",
        "Why we dream",
        "We dream predominantly during Rapid Eye Movement (REM) sleep when brain activity surges. Neuroscientists believe dreaming serves vital cognitive functions: consolidating new memories from short-term to permanent storage, processing emotional experiences, simulating threat scenarios for survival readiness, and neurocognitive problem-solving by forging creative associative connections."
    },
    {
        "how black holes form,how do black holes form,black hole formation,stellar collapse,supernova",
        "How black holes form",
        "A black hole forms when a massive star - at least twenty times more massive than the Sun - runs out of nuclear fuel. Without outward nuclear fusion pressure to counteract gravity, the star undergoes catastrophic core collapse, triggering a titanic supernova explosion. The remaining mass collapses into an infinitely dense gravitational singularity from which not even light can escape."
    },
    {
        "how vaccines work,how do vaccines work,vaccine,immunization,antibodies,mrna vaccine",
        "How vaccines work",
        "Vaccines work by training the immune system to recognize and fight pathogens safely. They introduce harmless antigens, such as inactivated virus fragments, weakened microbes, or mRNA instructions, without causing the disease. White blood cells produce specialized antibodies and long-lived memory B-cells and T-cells, enabling the immune system to rapidly neutralize the real pathogen if exposed in the future."
    },
    {
        "how fish breathe underwater,how do fish breathe underwater,how do fish breathe,fish gills,gills",
        "How fish breathe underwater",
        "Fish breathe underwater using specialized respiratory organs called gills. As a fish swims or gulps water, water passes over feather-like gill filaments packed with thousands of microscopic blood capillaries. Dissolved oxygen in the water diffuses through thin gill membranes directly into the fish's bloodstream, while carbon dioxide waste diffuses outward into the passing water."
    },
    {
        "why do bees make honey,why bees make honey,honey,beekeeping,beehive",
        "Why bees make honey",
        "Bees make honey as an energy-packed food store to survive winter and bad weather when flowers are not blooming. Worker bees collect sugary floral nectar, add digestive enzymes like invertase inside their honey stomachs to break complex sucrose into simple glucose and fructose, and spread it inside honeycomb cells, fanning their wings vigorously to evaporate moisture until it thickens into rich antibacterial honey."
    },
    {
        "why do onions make you cry,why onions make you cry,chopping onions,crying from onions",
        "Why onions make you cry",
        "Onions make you cry because cutting them ruptures plant cell walls, allowing alliinase enzymes to mix with amino acid sulfoxides. This biochemical reaction produces a volatile lachrymatory sulfur gas called syn-propanethial-S-oxide. When this gas drifts into your eyes, it reacts with natural eye moisture to form mild sulfuric acid, triggering tear glands to flush out the irritant."
    },
    {
        "why do stars twinkle,why stars twinkle,twinkling stars,atmospheric scintillation",
        "Why stars twinkle",
        "Stars twinkle because their pinpoint starlight must travel through layers of Earth's shifting atmosphere before reaching our eyes. Pockets of warm and cool air with varying air densities act like tiny shifting lenses, continually bending and refracting the narrow beam of light. This creates rapid fluctuations in perceived brightness and position called atmospheric scintillation."
    },
    {
        "why does ice float,why ice floats on water,ice floating,density of ice",
        "Why ice floats on water",
        "Ice floats on water because water expands and becomes less dense as it freezes - an unusual property driven by hydrogen bonding. Above four degrees Celsius, water molecules tumble closely together. As water cools toward freezing, hydrogen bonds lock the molecules into a rigid hexagonal open crystal lattice, spreading them farther apart and making ice approximately nine percent less dense than liquid water."
    },
    {
        "how earthquakes occur,how do earthquakes happen,earthquake,seismic waves,tectonic plates",
        "How earthquakes occur",
        "Earthquakes occur when tectonic plates grinding against each other get locked by friction along fault lines. As the plates continue pushing, elastic stress accumulates over decades or centuries. When the stress exceeds the rock's frictional strength, the fault suddenly slips, unleashing the stored energy as seismic shockwaves that ripple through the Earth's crust, causing the ground to shake violently."
    },
    {
        "what causes rainbows,how rainbows form,rainbow,rainbows,refraction,dispersion of light",
        "What causes rainbows",
        "Rainbows are optical phenomena formed when sunlight strikes suspended raindrops in the atmosphere. Each spherical raindrop acts as a miniature prism: as sunlight enters the droplet, it refracts and disperses into its constituent spectral colors, reflects off the back inside wall of the drop, and refracts again upon exiting, creating a circular 42-degree arc of vibrant colored light."
    },
    {
        "how do bats navigate in the dark,how bats navigate,echolocation,bat echolocation,bats blind",
        "How bats navigate in the dark",
        "Bats navigate in total darkness using biological sonar known as echolocation. A bat emits high-frequency ultrasonic chirps through its mouth or nose, often well above human hearing limits. When these sound waves hit nearby trees, caves, or flying insects, echoes bounce back. By processing the tiny differences in echo timing, pitch, and loudness between their two ears, bats construct a high-resolution 3D acoustic map of their surroundings."
    },
    {
        "how do magnets work,how magnets work,magnetism,magnetic field,magnets",
        "How magnets work",
        "Magnets work through the quantum mechanical spin and motion of electrons within atoms. In ferromagnetic materials like iron, nickel, and cobalt, microscopic regions called magnetic domains have electron spins that can be aligned in the same direction. When aligned, their magnetic dipoles add together to produce a macroscopic invisible magnetic field with north and south poles that exert forces on other magnetic dipoles."
    },
    {
        "how do solar panels work,how solar panels work,photovoltaic,solar energy,photovoltaic effect",
        "How solar panels work",
        "Solar panels work using the photovoltaic effect to convert sunlight directly into electricity. Sunlight is composed of energy packets called photons. When photons strike the specially treated silicon semiconductor layers within a solar cell, they knock electrons free from silicon atoms. Metal conductive plates on the cell channel these free electrons into a directional electric direct current that can power homes and batteries."
    },
    {
        "why do flamingos stand on one leg,why flamingos stand on one leg,flamingo,flamingos",
        "Why flamingos stand on one leg",
        "Flamingos stand on one leg primarily to conserve body heat and energy. Water and cold mud draw heat away from their bodies much faster than air, so tucking one bare leg against their feathered body reduces thermal loss. Furthermore, biomechanical studies show flamingos have a unique passive stay apparatus in their knee joints that locks their body into stable balance without active muscular effort."
    },
    {
        "why do chameleons change color,why chameleons change color,chameleon color change,chameleon",
        "Why chameleons change color",
        "Chameleons change color primarily for social communication, mood expression, and temperature regulation, rather than camouflage. Beneath their transparent outer skin are specialized cells called iridophores containing microscopic guanine nanocrystals. By relaxing or exciting their skin, chameleons shift the spacing between these nanocrystals, altering which wavelengths of light are reflected - turning bright yellow or red when courting or aggressive, and dark brown when cold."
    },
    {
        "why is yawning contagious,contagious yawning,why do we yawn,yawn",
        "Why yawning is contagious",
        "Contagious yawning is linked to social empathy and mirror neuron networks in the brain. When you see or hear someone yawn, mirror neurons in frontal cortex regions trigger empathetic mimicry. Evolutionary psychologists suggest contagious yawning helped ancient animal and human social groups synchronize sleep cycles, maintain collective vigilance, and cool the brain to promote alertness across the tribe."
    },
    {
        "how does gps work,how gps works,global positioning system,satellite navigation,gps",
        "How GPS navigation works",
        "The Global Positioning System works using a constellation of over thirty satellites orbiting Earth, each carrying atomic clocks precise to nanoseconds. GPS satellites continuously broadcast radio signals with their exact time and orbital coordinates. A GPS receiver picks up signals from at least four satellites simultaneously, calculating distance based on signal travel time at the speed of light to trilaterate your precise 3D latitude, longitude, and elevation."
    },
    {
        "quantum computing,quantum computer,qubits,superposition,quantum entanglement",
        "Quantum computing",
        "Quantum computing harnesses the strange principles of quantum mechanics to perform complex computations exponentially faster than classical computers. While classical computers store information as binary bits of zeros and ones, quantum processors use quantum bits, or qubits, which can exist in a superposition of both states simultaneously. Through quantum entanglement and interference, quantum algorithms can evaluate astronomical combinatorial possibilities at once."
    }
};

static int search_offline_knowledge(const char *subject, SearchResult *out_result) {
    if (!subject || !out_result) return 0;
    char lower_subj[256];
    size_t i = 0;
    for (; subject[i] && i + 1 < sizeof(lower_subj); i++) {
        lower_subj[i] = (char)tolower((unsigned char)subject[i]);
    }
    lower_subj[i] = '\0';

    int num_articles = (int)(sizeof(OFFLINE_KB) / sizeof(OFFLINE_KB[0]));
    int best_match_idx = -1;
    size_t best_match_len = 0;

    // Pass 1: Exact match with any keyword alias
    for (int a = 0; a < num_articles; a++) {
        const char *kw = OFFLINE_KB[a].keywords;
        while (*kw) {
            const char *comma = strchr(kw, ',');
            size_t klen = comma ? (size_t)(comma - kw) : strlen(kw);
            if (strlen(lower_subj) == klen && strncmp(lower_subj, kw, klen) == 0) {
                strncpy(out_result->title, OFFLINE_KB[a].title, sizeof(out_result->title) - 1);
                strncpy(out_result->extract, OFFLINE_KB[a].extract, sizeof(out_result->extract) - 1);
                out_result->has_image = 0;
                return 1;
            }
            if (!comma) break;
            kw = comma + 1;
        }
    }

    // Pass 2: Substring matching (prefer longer, more specific matches)
    for (int a = 0; a < num_articles; a++) {
        const char *kw = OFFLINE_KB[a].keywords;
        while (*kw) {
            const char *comma = strchr(kw, ',');
            size_t klen = comma ? (size_t)(comma - kw) : strlen(kw);
            char single_kw[128];
            if (klen < sizeof(single_kw)) {
                memcpy(single_kw, kw, klen);
                single_kw[klen] = '\0';
                if (klen >= 4 && strstr(lower_subj, single_kw) != NULL) {
                    if (klen > best_match_len) {
                        best_match_len = klen;
                        best_match_idx = a;
                    }
                } else if (strlen(lower_subj) >= 4 && strstr(single_kw, lower_subj) != NULL) {
                    if (strlen(lower_subj) > best_match_len) {
                        best_match_len = strlen(lower_subj);
                        best_match_idx = a;
                    }
                }
            }
            if (!comma) break;
            kw = comma + 1;
        }
    }

    if (best_match_idx >= 0) {
        strncpy(out_result->title, OFFLINE_KB[best_match_idx].title, sizeof(out_result->title) - 1);
        strncpy(out_result->extract, OFFLINE_KB[best_match_idx].extract, sizeof(out_result->extract) - 1);
        out_result->has_image = 0;
        return 1;
    }

    return 0;
}

static void to_lower_str(const char *src, char *dst, size_t max_len) {
    size_t i = 0;
    for (; src[i] && i + 1 < max_len; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static int is_title_relevant(const char *query_subject, const char *wiki_title) {
    if (!query_subject || !wiki_title || strlen(query_subject) == 0 || strlen(wiki_title) == 0) return 0;
    char q_lower[256], t_lower[256];
    to_lower_str(query_subject, q_lower, sizeof(q_lower));
    to_lower_str(wiki_title, t_lower, sizeof(t_lower));

    if (strstr(t_lower, q_lower) != NULL || strstr(q_lower, t_lower) != NULL) {
        return 1;
    }

    char q_copy[256];
    strncpy(q_copy, q_lower, sizeof(q_copy) - 1);
    q_copy[sizeof(q_copy) - 1] = '\0';

    char *token = strtok(q_copy, " ,.-'\"?!");
    while (token) {
        if (strlen(token) >= 3) {
            if (strcmp(token, "the") != 0 && strcmp(token, "and") != 0 && strcmp(token, "for") != 0 &&
                strcmp(token, "can") != 0 && strcmp(token, "you") != 0 && strcmp(token, "how") != 0 &&
                strcmp(token, "what") != 0 && strcmp(token, "who") != 0 && strcmp(token, "why") != 0 &&
                strcmp(token, "when") != 0 && strcmp(token, "where") != 0 && strcmp(token, "about") != 0 &&
                strcmp(token, "tell") != 0 && strcmp(token, "teach") != 0 && strcmp(token, "learn") != 0 &&
                strcmp(token, "with") != 0 && strcmp(token, "that") != 0 && strcmp(token, "this") != 0) {
                if (strstr(t_lower, token) != NULL) {
                    return 1;
                }
            }
        }
        token = strtok(NULL, " ,.-'\"?!");
    }
    return 0;
}

int search_web_and_images(const char *query, SearchResult *out_result) {
    if (!query || !out_result) return 0;
    memset(out_result, 0, sizeof(SearchResult));

    char subject[512];
    extract_subject(query, subject, sizeof(subject));
    if (strlen(subject) == 0) {
        strncpy(subject, query, sizeof(subject) - 1);
        subject[sizeof(subject) - 1] = '\0';
    }

    // Step 0: Check verified offline knowledge base first (0ms instant response)
    if (search_offline_knowledge(subject, out_result) || search_offline_knowledge(query, out_result)) {
        return 1;
    }

    char encoded_subj[512];
    url_encode(subject, encoded_subj, sizeof(encoded_subj));

    char canonical_title[256] = {0};
    int got_title = 0;

    // Step 1: OpenSearch for exact canonical encyclopedia title with identified User-Agent
    char open_cmd[2048];
    snprintf(open_cmd, sizeof(open_cmd),
        "curl.exe -s -L -A \"SphereAI/2.0 (desktop-client; contact@sphere.internal)\" --connect-timeout 2 --max-time 4 "
        "\"https://en.wikipedia.org/w/api.php?action=opensearch&search=%s&limit=1&namespace=0&format=json\"",
        encoded_subj);

    FILE *pipe = _popen(open_cmd, "r");
    if (pipe) {
        char *resp = (char *)malloc(16384);
        if (resp) {
            size_t n = fread(resp, 1, 16383, pipe);
            resp[n] = '\0';
            if (extract_opensearch_title(resp, canonical_title, sizeof(canonical_title))) {
                if (strstr(canonical_title, "Closed-ended question") == NULL) {
                    got_title = 1;
                }
            }
            free(resp);
        }
        _pclose(pipe);
    }

    // Step 1b: If OpenSearch didn't find title, search via Wikipedia list=search
    if (!got_title) {
        char list_cmd[2048];
        snprintf(list_cmd, sizeof(list_cmd),
            "curl.exe -s -L -A \"SphereAI/2.0 (desktop-client; contact@sphere.internal)\" --connect-timeout 2 --max-time 4 "
            "\"https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch=%s&srlimit=3&format=json\"",
            encoded_subj);
        pipe = _popen(list_cmd, "r");
        if (pipe) {
            char *resp = (char *)malloc(32768);
            if (resp) {
                size_t n = fread(resp, 1, 32767, pipe);
                resp[n] = '\0';
                char found_t[256] = {0};
                if (extract_json_field(resp, "title", found_t, sizeof(found_t))) {
                    if (is_title_relevant(subject, found_t)) {
                        snprintf(canonical_title, sizeof(canonical_title), "%s", found_t);
                        got_title = 1;
                    }
                }
                free(resp);
            }
            _pclose(pipe);
        }
    }

    // Step 2: Fetch exact extract and thumbnail for canonical title
    char fetch_cmd[2048];
    if (got_title) {
        char enc_title[512];
        url_encode(canonical_title, enc_title, sizeof(enc_title));
        snprintf(fetch_cmd, sizeof(fetch_cmd),
            "curl.exe -s -L -A \"SphereAI/2.0 (desktop-client; contact@sphere.internal)\" --connect-timeout 2 --max-time 4 "
            "\"https://en.wikipedia.org/w/api.php?action=query&titles=%s&prop=extracts|pageimages&exintro=1&explaintext=1&piprop=original|thumbnail&redirects=1&format=json\"",
            enc_title);
    } else {
        snprintf(fetch_cmd, sizeof(fetch_cmd),
            "curl.exe -s -L -A \"SphereAI/2.0 (desktop-client; contact@sphere.internal)\" --connect-timeout 2 --max-time 4 "
            "\"https://en.wikipedia.org/w/api.php?action=query&generator=search&gsrsearch=%s&gsrlimit=1&prop=pageimages|extracts&exintro=1&explaintext=1&piprop=original|thumbnail&format=json\"",
            encoded_subj);
    }

    pipe = _popen(fetch_cmd, "r");
    if (pipe) {
        char *response = (char *)malloc(65536);
        if (response) {
            size_t bytes_read = fread(response, 1, 65535, pipe);
            response[bytes_read] = '\0';

            if (extract_json_field(response, "title", out_result->title, sizeof(out_result->title))) {
                // If title was from fuzzy generator search, strictly verify relevance against the query
                if (!got_title && !is_title_relevant(subject, out_result->title)) {
                    free(response);
                    _pclose(pipe);
                    return search_offline_knowledge(subject, out_result);
                }

                extract_json_field(response, "extract", out_result->extract, sizeof(out_result->extract));

                if (extract_json_field(response, "source", out_result->image_url, sizeof(out_result->image_url))) {
                    out_result->has_image = 1;
                    clean_image_caption(out_result->image_url, out_result->image_caption, sizeof(out_result->image_caption));
                    snprintf(out_result->visual_details, sizeof(out_result->visual_details),
                             "Visual photograph of %s. The imagery exhibits high-clarity focal focus, authentic lighting, and definitive contours of %s.",
                             out_result->image_caption, out_result->title);
                }

                free(response);
                _pclose(pipe);

                if (strlen(out_result->extract) > 15) {
                    return 1;
                }
            } else {
                free(response);
            }
        }
        _pclose(pipe);
    }

    // Step 3: Seamless offline fallback if online query didn't return a match
    return search_offline_knowledge(subject, out_result);
}

int search_music_track(const char *query, MusicTrack *out_track) {
    if (!query || !out_track) return 0;
    memset(out_track, 0, sizeof(MusicTrack));

    // Strip conversational prefixes: "play ", "play song ", "play me ", etc.
    const char *start = query;
    while (*start && isspace((unsigned char)*start)) start++;

    char lower_q[512];
    to_lower_str(start, lower_q, sizeof(lower_q));

    const char *p = lower_q;
    const char *prefixes[] = {
        "hey sphere play the song ", "hey sphere play a song ", "hey sphere play song ", "hey sphere play me ", "hey sphere play track ", "hey sphere play ",
        "sphere play the song ", "sphere play a song ", "sphere play song ", "sphere play me ", "sphere play track ", "sphere play ",
        "play the song ", "play a song ", "play song ", "play me the song ", "play me a song ", "play me ", "play track ", "play music ", "play "
    };
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++) {
        size_t len = strlen(prefixes[i]);
        if (strncmp(p, prefixes[i], len) == 0) {
            start += len;
            break;
        }
    }

    char clean[512];
    size_t d = 0;
    while (start[d] && d + 1 < sizeof(clean)) {
        clean[d] = start[d];
        d++;
    }
    clean[d] = '\0';

    while (d > 0 && (clean[d-1] == '?' || clean[d-1] == '.' || clean[d-1] == '!' || clean[d-1] == ' ' || clean[d-1] == '"' || clean[d-1] == '\'')) {
        clean[--d] = '\0';
    }

    if (d == 0) {
        strncpy(clean, "Blinding Lights The Weeknd", sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';
    }

    char temp_dir[MAX_PATH] = "C:\\Windows\\Temp\\";
#ifdef _WIN32
    GetTempPathA(MAX_PATH, temp_dir);
#else
    const char *t = getenv("TMPDIR");
    if (!t) t = "/tmp/";
    strncpy(temp_dir, t, sizeof(temp_dir) - 1);
#endif
    char song_path[MAX_PATH];
    snprintf(song_path, sizeof(song_path), "%ssphere_song.m4a", temp_dir);

    // Escape characters in search query
    char safe_query[512];
    size_t sq = 0;
    for (size_t k = 0; clean[k] && sq + 2 < sizeof(safe_query); k++) {
        if (clean[k] == '"' || clean[k] == '%' || clean[k] == '&' || clean[k] == '|' || clean[k] == '<' || clean[k] == '>') continue;
        safe_query[sq++] = clean[k];
    }
    safe_query[sq] = '\0';

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "yt-dlp.exe --print \"%%(title)s\" \"ytsearch1:%s audio\" -f \"ba[ext=m4a]/ba\" -o \"%s\" --no-playlist --no-warnings --force-overwrites",
        safe_query, song_path);

    FILE *pipe = _popen(cmd, "r");
    char raw_title[512] = {0};
    if (pipe) {
        if (fgets(raw_title, sizeof(raw_title), pipe)) {
            size_t l = strlen(raw_title);
            while (l > 0 && (raw_title[l-1] == '\n' || raw_title[l-1] == '\r')) {
                raw_title[--l] = '\0';
            }
        }
        _pclose(pipe);
    }

    // Verify downloaded full song file exists and has valid audio size (> 100 KB)
    FILE *chk = fopen(song_path, "rb");
    if (!chk) {
        return 0;
    }
    fseek(chk, 0, SEEK_END);
    long fsize = ftell(chk);
    fclose(chk);

    if (fsize < 100000) {
        return 0;
    }

    strncpy(out_track->preview_url, song_path, sizeof(out_track->preview_url) - 1);

    if (strlen(raw_title) > 0) {
        // Strip YouTube annotations in brackets or parentheses
        char clean_title[512];
        size_t c_idx = 0;
        int in_paren = 0;
        int in_brack = 0;
        for (size_t i = 0; raw_title[i] && c_idx + 1 < sizeof(clean_title); i++) {
            char ch = raw_title[i];
            if (ch == '(') in_paren++;
            else if (ch == ')') { if (in_paren > 0) in_paren--; }
            else if (ch == '[') in_brack++;
            else if (ch == ']') { if (in_brack > 0) in_brack--; }
            else if (!in_paren && !in_brack) {
                clean_title[c_idx++] = ch;
            }
        }
        clean_title[c_idx] = '\0';

        while (c_idx > 0 && (clean_title[c_idx-1] == ' ' || clean_title[c_idx-1] == '-' || clean_title[c_idx-1] == '_')) {
            clean_title[--c_idx] = '\0';
        }

        // Check for artist separator: " - " or en-dash/em-dash
        char *sep = strstr(clean_title, " - ");
        if (!sep) sep = strstr(clean_title, " – ");
        if (!sep) sep = strstr(clean_title, " — ");

        if (sep) {
            *sep = '\0';
            char *art = clean_title;
            char *trk = sep + 3;
            while (*trk == ' ' || *trk == '-' || (unsigned char)*trk > 127) trk++;
            while (*art == ' ') art++;

            snprintf(out_track->artist_name, sizeof(out_track->artist_name), "%s", art);
            snprintf(out_track->track_name, sizeof(out_track->track_name), "%s", trk);
        } else {
            snprintf(out_track->track_name, sizeof(out_track->track_name), "%s", clean_title);
            snprintf(out_track->artist_name, sizeof(out_track->artist_name), "%s", "Featured Artist");
        }
    } else {
        snprintf(out_track->track_name, sizeof(out_track->track_name), "%s", clean);
        snprintf(out_track->artist_name, sizeof(out_track->artist_name), "%s", "Featured Artist");
    }

    out_track->found = 1;
    return 1;
}
