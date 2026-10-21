#define _USE_MATH_DEFINES
#include "brain.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

static void to_lower_str(const char *src, char *dst, size_t max_len) {
    size_t i = 0;
    for (; src[i] && i + 1 < max_len; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static int contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

int brain_evaluate_math(const char *query, char *out_answer, size_t max_len) {
    char lower[512];
    to_lower_str(query, lower, sizeof(lower));

    double num1 = 0.0, num2 = 0.0;
    char op = 0;
    int is_math = 0;

    if (contains(lower, "square root of") || contains(lower, "sqrt")) {
        const char *p = strstr(lower, "root of");
        if (p) p += 7;
        else p = strstr(lower, "sqrt");
        if (p) {
            while (*p && !isdigit((unsigned char)*p) && *p != '.') p++;
            if (*p && sscanf(p, "%lf", &num1) == 1) {
                if (num1 >= 0) {
                    double res = sqrt(num1);
                    snprintf(out_answer, max_len, "The square root of %g is %g.", num1, res);
                    return 1;
                } else {
                    snprintf(out_answer, max_len, "The square root of a negative number is imaginary.");
                    return 1;
                }
            }
        }
    }

    if (contains(lower, "percent of") || contains(lower, "% of")) {
        const char *p = lower;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (*p && sscanf(p, "%lf", &num1) == 1) {
            const char *p2 = strstr(p, "of");
            if (p2) {
                p2 += 2;
                while (*p2 && !isdigit((unsigned char)*p2) && *p2 != '-') p2++;
                if (*p2 && sscanf(p2, "%lf", &num2) == 1) {
                    double res = (num1 / 100.0) * num2;
                    snprintf(out_answer, max_len, "%g percent of %g is %g.", num1, num2, res);
                    return 1;
                }
            }
        }
    }

    const char *p = lower;
    while (*p && !isdigit((unsigned char)*p) && *p != '-') p++;
    if (*p && sscanf(p, "%lf", &num1) == 1) {
        while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == '-')) p++;
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p == '*' || *p == 'x' || strncmp(p, "times", 5) == 0 || strncmp(p, "multiplied by", 13) == 0) {
            op = '*';
            is_math = 1;
        } else if (*p == '/' || strncmp(p, "divided by", 10) == 0 || strncmp(p, "over", 4) == 0) {
            op = '/';
            is_math = 1;
        } else if (*p == '+' || strncmp(p, "plus", 4) == 0 || strncmp(p, "add", 3) == 0) {
            op = '+';
            is_math = 1;
        } else if (*p == '-' || strncmp(p, "minus", 5) == 0 || strncmp(p, "subtract", 8) == 0) {
            op = '-';
            is_math = 1;
        } else if (*p == '^' || strncmp(p, "power", 5) == 0 || strncmp(p, "to the power of", 15) == 0) {
            op = '^';
            is_math = 1;
        }

        if (is_math) {
            while (*p && !isdigit((unsigned char)*p) && *p != '-') p++;
            if (*p && sscanf(p, "%lf", &num2) == 1) {
                double result = 0.0;
                const char *op_word = "and";
                switch (op) {
                    case '*':
                        result = num1 * num2;
                        op_word = "multiplied by";
                        break;
                    case '/':
                        if (fabs(num2) < 1e-9) {
                            snprintf(out_answer, max_len, "Division by zero is undefined.");
                            return 1;
                        }
                        result = num1 / num2;
                        op_word = "divided by";
                        break;
                    case '+':
                        result = num1 + num2;
                        op_word = "plus";
                        break;
                    case '-':
                        result = num1 - num2;
                        op_word = "minus";
                        break;
                    case '^':
                        result = pow(num1, num2);
                        op_word = "to the power of";
                        break;
                }
                snprintf(out_answer, max_len, "%g %s %g equals %g.", num1, op_word, num2, result);
                return 1;
            }
        }
    }

    return 0;
}

static void clean_extract(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    size_t d = 0;
    int sentence_count = 0;

    for (size_t s = 0; src[s] && d + 1 < max_len; s++) {
        char c = src[s];

        if (c == '(') {
            int depth = 1;
            size_t lookahead = s + 1;
            while (src[lookahead] && depth > 0) {
                if (src[lookahead] == '(') depth++;
                else if (src[lookahead] == ')') depth--;
                lookahead++;
            }
            if (depth == 0) {
                s = lookahead - 1;
                continue;
            }

            continue;
        }

        if (c == '[') {
            int depth = 1;
            size_t lookahead = s + 1;
            while (src[lookahead] && depth > 0) {
                if (src[lookahead] == '[') depth++;
                else if (src[lookahead] == ']') depth--;
                lookahead++;
            }
            if (depth == 0) {
                s = lookahead - 1;
                continue;
            }
            continue;
        }

        if (c == ')' || c == ']') continue;

        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && d > 0 && dst[d - 1] == ' ') continue;

        if ((c == ',' || c == '.' || c == ';' || c == '!' || c == ':') && d > 0 && dst[d - 1] == ' ') {
            d--;
        }

        dst[d++] = c;

        if (c == '.' || c == '!') {
            if (isdigit((unsigned char)src[s + 1])) continue;

            if (s >= 1 && isalpha((unsigned char)src[s - 1]) && isalpha((unsigned char)src[s + 1])) continue;

            sentence_count++;
            if (sentence_count >= 3 && d > 320) {
                break;
            }
            if (sentence_count >= 4 && d > 200) {
                break;
            }
        }
    }

    while (d > 0 && (dst[d - 1] == ' ' || dst[d - 1] == '\n' || dst[d - 1] == '\r')) {
        d--;
    }
    dst[d] = '\0';
}

static void extract_clean_subject(const char *raw, char *clean, size_t max_len) {
    if (!raw || !clean || max_len == 0) return;
    clean[0] = '\0';

    const unsigned char *src = (const unsigned char *)raw;
    if (src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        src += 3;
    }

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
        "what is the ", "what is a ", "what is an ", "what is ",
        "what was the ", "what was a ", "what was an ", "what was ",
        "what does a ", "what does an ", "what does the ", "what does ",
        "what do a ", "what do an ", "what do the ", "what do ",
        "what are the ", "what are a ", "what are ",
        "who is the ", "who is a ", "who is an ", "who is ",
        "who was the ", "who was a ", "who was an ", "who was ",
        "who were the ", "who were ",
        "how tall is the ", "how tall is ", "how old is the ", "how old is ",
        "how big is the ", "how big is ", "how does a ", "how does an ", "how does the ", "how does ",
        "tell me about a ", "tell me about an ", "tell me about the ", "tell me about ",
        "tell me what a ", "tell me what is ",
        "describe a ", "describe an ", "describe the ", "describe ",
        "explain a ", "explain an ", "explain the ", "explain ",
        "picture of a ", "picture of an ", "picture of "
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
        " look like", " looks like", " looking like", " appear like", " appear", " seem like",
        " mean", " means", " work", " works"
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

void brain_describe_visual(const char *user_query, const char *subject_title, const char *image_caption,
                           const char *web_extract, char *out_desc, size_t max_len) {
    (void)image_caption;
    char lower_query[512];
    to_lower_str(user_query, lower_query, sizeof(lower_query));

    char subject[256] = {0};
    if (subject_title && strlen(subject_title) > 0) {
        strncpy(subject, subject_title, sizeof(subject) - 1);
    } else {
        extract_clean_subject(user_query, subject, sizeof(subject));
    }
    if (strlen(subject) == 0) {
        strncpy(subject, "this subject", sizeof(subject) - 1);
    }

    char lower_subj[256];
    to_lower_str(subject, lower_subj, sizeof(lower_subj));

    char visual_core[2048] = {0};
    char cleaned_fact[2048] = {0};
    if (web_extract && strlen(web_extract) > 0) {
        clean_extract(web_extract, cleaned_fact, sizeof(cleaned_fact));
    }

    if (contains(lower_subj, "computer mouse") || contains(lower_subj, "pc mouse") ||
        contains(lower_query, "computer mouse") || contains(lower_query, "pc mouse") ||
        contains(lower_subj, "optical mouse") || contains(lower_subj, "trackball") ||
        contains(lower_subj, "gaming mouse")) {
        snprintf(visual_core, sizeof(visual_core),
            "A computer mouse is an ergonomic handheld pointing device contoured to fit comfortably beneath the human palm. "
            "It features an arched plastic or composite chassis with two primary left and right click buttons, split by a grooved tactile rubber scroll wheel in the center. "
            "Its underside is equipped with low-friction PTFE glide pads flanking an optical LED or invisible infrared laser sensor aperture that tracks surface movement at thousands of DPI. "
            "It connects either via a flexible braided USB cable extending from its front or operates wirelessly with a sleek rechargeable battery chassis.");

    } else if (contains(lower_subj, "mouse") || contains(lower_subj, "mice") || contains(lower_subj, "rodent") ||
               contains(lower_subj, "house mouse") || contains(lower_subj, "field mouse") || contains(lower_query, "rodent")) {
        snprintf(visual_core, sizeof(visual_core),
            "A biological mouse is a small, nimble mammal and rodent. "
            "Its body is covered in a dense coat of soft, fine fur, typically shades of gray, dusty brown, or white, with a lighter underbelly. "
            "Its face is defined by a slender, pointed triangular snout tipped with a moist pink nose and long, twitching sensory whiskers. "
            "On its head are alert, rounded, translucent ears and glossy dark bead-like eyes. "
            "It moves on miniature four-toed front paws and five-toed hind feet equipped with delicate claws, terminating in a long, thin, scaly cylindrical tail nearly equal to the length of its body used for balance.");

    } else if (contains(lower_subj, "keyboard") || contains(lower_query, "keyboard")) {
        snprintf(visual_core, sizeof(visual_core),
            "A computer keyboard is a horizontal rectangular input deck arranged with staggered rows of tiered keycaps representing alphanumeric characters and functions. "
            "Each keycap is seated atop an individual mechanical switch or low-profile scissor membrane with distinct tactile travel, frequently accented by programmable per-key RGB backlighting and framed in an anodized aluminum top plate.");

    } else if (contains(lower_subj, "monitor") || contains(lower_subj, "display") || contains(lower_query, "computer monitor")) {
        snprintf(visual_core, sizeof(visual_core),
            "A computer monitor features a thin rectangular flat or gently curved panel framed by micro-edge bezels, mounted to an adjustable weighted pedestal stand. "
            "Its front panel has an anti-glare matte coating that illuminates millions of microscopic LED, IPS, or OLED pixels to render high-resolution digital imagery, backed by display ports and cooling vents on the rear.");

    } else if (contains(lower_subj, "laptop") || contains(lower_subj, "notebook") || contains(lower_subj, "macbook")) {
        snprintf(visual_core, sizeof(visual_core),
            "A laptop computer is a sleek, portable clamshell system engineered with a hinged aluminum or composite chassis. "
            "When opened, it reveals an ultra-thin widescreen LED display embedded in the upper lid, paired with a backlit chiclet keyboard and a smooth multi-touch glass trackpad integrated into the lower palm deck.");

    } else if (contains(lower_subj, "smartphone") || contains(lower_subj, "phone") || contains(lower_subj, "iphone") || contains(lower_subj, "android")) {
        snprintf(visual_core, sizeof(visual_core),
            "A modern smartphone features a slim handheld slate profile bounded by rounded aerospace aluminum or titanium rails, fronted by an edge-to-edge tempered glass touchscreen. "
            "On the rear frosted glass surface sits a raised multi-lens camera module with optical lenses, flash, and sensor apertures.");

    } else if (contains(lower_subj, "headphone") || contains(lower_subj, "headset") || contains(lower_subj, "earphone")) {
        snprintf(visual_core, sizeof(visual_core),
            "Headphones consist of two cushioned acoustic earcups lined with plush memory foam and leatherette, contoured to envelope or sit against the ears. "
            "The earcups are suspended from an adjustable padded steel headband, integrating dynamic speaker drivers and micro-mesh acoustic grilles.");

    } else if (contains(lower_subj, "car") || contains(lower_subj, "automobile") || contains(lower_subj, "vehicle") || contains(lower_subj, "ferrari") || contains(lower_subj, "porsche")) {
        snprintf(visual_core, sizeof(visual_core),
            "A motor car features a low-slung, aerodynamic metallic chassis engineered for dynamic airflow. "
            "From the front, a sloping sculpted hood extends towards an intake grille, flanked by sharp glass headlamps housing projector optics. "
            "The cabin forms a central passenger cockpit enclosed by expansive curved safety glass, framed by high-tensile roof pillars and side mirrors. "
            "Along the sides, crisp character body lines run towards pronounced wheel arches housing circular rubber tires with radial tread patterns wrapped around alloy rims.");

    } else if (contains(lower_subj, "airplane") || contains(lower_subj, "plane") || contains(lower_subj, "aircraft") || contains(lower_subj, "jet")) {
        snprintf(visual_core, sizeof(visual_core),
            "An aircraft features a streamlined cylindrical pressurized fuselage tapering to a conical nose and vertical empennage. "
            "Extending horizontally are high-aspect-ratio swept aerodynamic wings fitted with winglets and underslung turbofan engines. "
            "The surface gleams with smooth, reflective aviation composites punctuated by transparent cockpit canopies and navigation strobe lights.");

    } else if (contains(lower_subj, "tiger") || contains(lower_subj, "lion") || contains(lower_subj, "cheetah") || contains(lower_subj, "leopard")) {
        snprintf(visual_core, sizeof(visual_core),
            "A magnificent apex predator with a muscular, agile quadrupedal anatomy, broad chest, and powerful shoulders. "
            "Its dense coat is patterned with distinctive camouflage markings, framing an intense cranial structure with forward-facing amber eyes, sensitive whiskers, and broad padded paws with lethal retractable claws.");

    } else if (contains(lower_subj, "cat") || contains(lower_subj, "kitten") || contains(lower_subj, "feline")) {
        snprintf(visual_core, sizeof(visual_core),
            "A domestic cat is a lithe, graceful small carnivore with a flexible spine, soft dense fur, and silent padded paws with retractable claws. "
            "Its expressive face is framed by erect triangular ears, luminous forward-facing eyes with slit pupils, and delicate sensory whiskers radiating from its muzzle.");

    } else if (contains(lower_subj, "dog") || contains(lower_subj, "puppy") || contains(lower_subj, "canine")) {
        snprintf(visual_core, sizeof(visual_core),
            "A domestic dog possesses an athletic mammalian build with a fur-covered coat, an expressive snout tipped with a moist sensory nose pad, communicative ears that may be perky or flopped, and an energetic wagging tail.");

    } else if (contains(lower_subj, "spider") || contains(lower_subj, "arachnid") || contains(lower_query, "spider")) {
        snprintf(visual_core, sizeof(visual_core),
            "A spider is an eight-legged predatory arachnid characterized by a two-part anatomy: an unsegmented cephalothorax fused to a bulbous abdomen. "
            "Its head is clustered with multiple glossy obsidian simple eyes above downward-pointing chelicerae fangs. "
            "Radiating from its body are four pairs of articulated jointed legs coated in sensitive acoustic setae hairs and tiny tarsal claws, with specialized silk-spinning spinnerets located at the posterior tip of its abdomen.");

    } else if (contains(lower_subj, "sunflower") || contains(lower_subj, "flower") || contains(lower_subj, "rose") || contains(lower_subj, "tulip")) {
        snprintf(visual_core, sizeof(visual_core),
            "A vibrant botanical specimen dominated by colorful radiating petals arranged symmetrically around a central seed disc. "
            "It is supported by a slender, fibrous green stem dressed with textured leaves, showcasing vivid organic pigmentation designed to attract pollinators.");

    } else if (contains(lower_subj, "tree") || contains(lower_subj, "oak") || contains(lower_subj, "pine") || contains(lower_subj, "forest")) {
        snprintf(visual_core, sizeof(visual_core),
            "A majestic perennial plant anchored by a thick, vertical wooden trunk wrapped in deeply fissured, textured bark. "
            "From the trunk, sprawling woody branches divide outwards into a towering fractal canopy filled with vibrant green foliage or needle-like leaves.");

    } else if (contains(lower_subj, "black hole") || contains(lower_subj, "singularity")) {
        snprintf(visual_core, sizeof(visual_core),
            "A black hole is marked by the event horizon - a pitch-black, perfect gravitational shadow in deep space where no light can escape. "
            "Surrounding this absolute void is a superheated, swirling accretion disk of plasma and cosmic dust that glows with radiant amber and sapphire hues, framed by a halo of gravitationally bent starlight.");

    } else if (contains(lower_subj, "galaxy") || contains(lower_subj, "nebula") || contains(lower_subj, "space")) {
        snprintf(visual_core, sizeof(visual_core),
            "A spiral galaxy appears as a vast, luminous cosmic vortex floating against the deep void. "
            "At its center shines a dense, warm golden core of ancient stars, out of which graceful curved spiral arms sweep across light-years, illuminated by sapphire newborn stars and veined with ribbons of dark interstellar dust.");

    } else if (contains(lower_subj, "eiffel") || contains(lower_subj, "tower") || contains(lower_subj, "pyramid") || contains(lower_subj, "building")) {
        snprintf(visual_core, sizeof(visual_core),
            "A monumental architectural landmark defined by towering verticality and intricate geometric engineering. "
            "Its structure rises from expansive foundation arches, tapering gracefully through elevated platforms towards an illuminated summit spire casting dramatic silhouettes against the skyline.");

    } else {
        if (strlen(cleaned_fact) > 0) {
            snprintf(out_desc, max_len, "Visually, %s: %s", subject, cleaned_fact);
            return;
        } else {
            snprintf(out_desc, max_len, "%s has distinct physical visual characteristics. Let me know what specific details you would like to explore.", subject);
            return;
        }
    }

    if (strlen(cleaned_fact) > 0) {
        snprintf(out_desc, max_len, "%s %s", visual_core, cleaned_fact);
    } else {
        snprintf(out_desc, max_len, "%s", visual_core);
    }
}

void brain_explain_general(const char *query, const char *subject_title, const char *web_extract, char *out_answer, size_t max_len) {
    char cleaned[2048] = {0};
    if (web_extract && strlen(web_extract) > 0) {
        clean_extract(web_extract, cleaned, sizeof(cleaned));
    }

    char lower_q[512];
    to_lower_str(query, lower_q, sizeof(lower_q));

    int is_teaching = (contains(lower_q, "teach me") || contains(lower_q, "learn") ||
                       contains(lower_q, "tutorial") || contains(lower_q, "how to understand") ||
                       contains(lower_q, "help me with") || contains(lower_q, "can you teach"));

    if (is_teaching && strlen(cleaned) > 0) {
        const char *subj = (subject_title && strlen(subject_title) > 0) ? subject_title : "this topic";
        if (contains(lower_q, "calculus")) {
            snprintf(out_answer, max_len,
                "Yes, I would love to teach you calculus! %s "
                "Calculus has two main pillars: differential calculus for rates of change and slopes, and integral calculus for accumulations and areas. Would you like to start with derivatives or integrals?",
                cleaned);
            return;
        }
        snprintf(out_answer, max_len,
            "Yes, absolutely! I can teach you all about %s. %s What specific concept would you like to explore first?",
            subj, cleaned);
        return;
    }

    if (strlen(cleaned) > 0) {
        if (contains(lower_q, "why do") || contains(lower_q, "why does") || contains(lower_q, "why is") || contains(lower_q, "why are") || contains(lower_q, "why ")) {
            snprintf(out_answer, max_len,
                "Here is why this happens: %s It is a fascinating example of natural mechanics! What other curious questions do you have?",
                cleaned);
            return;
        }
        if (contains(lower_q, "how do") || contains(lower_q, "how does") || contains(lower_q, "how is") || contains(lower_q, "how are") || contains(lower_q, "how can")) {
            snprintf(out_answer, max_len,
                "Here is how that works: %s Remarkable principles in action! Would you like to dive deeper into any part of this?",
                cleaned);
            return;
        }
        if (contains(cleaned, "?") || strlen(cleaned) > 350) {
            snprintf(out_answer, max_len, "%s", cleaned);
        } else {
            snprintf(out_answer, max_len, "%s What other aspects of this would you like to explore?", cleaned);
        }
        return;
    }

    const char *subj = (subject_title && strlen(subject_title) > 0) ? subject_title : query;
    snprintf(out_answer, max_len,
        "While I don't have an exact encyclopedic article on \"%s\", conceptually it involves distinct principles and tradeoffs. Tell me what specific angle or questions you're exploring, and we can reason through it together!",
        subj);
}

static int brain_handle_customization_and_personality(const char *lower, char *out_response, size_t max_len, int *out_is_happy) {
    if (contains(lower, "change the ball") || contains(lower, "change ball") ||
        contains(lower, "customize the ball") || contains(lower, "customise the ball") ||
        contains(lower, "customize ball") || contains(lower, "ball options") ||
        contains(lower, "options for the ball") || contains(lower, "change my ball")) {
        snprintf(out_response, max_len,
            "I'd love to customize the ball with you! Here are all the options you can choose from:\n"
            "1. Ball Colors: Red, Electric Blue, Emerald Green, Lime, Vivid Yellow, Gold, Mystic Purple, Cyan Aqua, Neon Pink, Sunset Orange, or Crystal White.\n"
            "2. 3D Facial Expressions: Happy Smile, Sad Frown, Surprised, Angry, Cheeky Wink, or Cool with Sunglasses.\n"
            "3. Reset: Say 'reset color' or 'neutral face' anytime.\n"
            "Just tell me what you'd like, for example: 'make the ball blue', 'show an angry face', or 'turn pink'!");
        return 1;
    }

    int is_color_cmd = (contains(lower, "color") || contains(lower, "colour") ||
                        contains(lower, "make the ball") || contains(lower, "make ball") ||
                        contains(lower, "turn the ball") || contains(lower, "turn ball") ||
                        contains(lower, "change the ball to") || contains(lower, "set ball to") ||
                        contains(lower, "set color to") || contains(lower, "change color to"));

    if (is_color_cmd || contains(lower, "ball red") || contains(lower, "ball blue") ||
        contains(lower, "ball green") || contains(lower, "ball yellow") || contains(lower, "ball purple") ||
        contains(lower, "ball cyan") || contains(lower, "ball pink") || contains(lower, "ball orange") ||
        contains(lower, "ball white")) {
        if (contains(lower, "red") || contains(lower, "crimson") || contains(lower, "ruby")) {
            renderer_set_custom_color(COLOR_RED);
            snprintf(out_response, max_len, "Changing my particle sphere to vibrant crimson red! A powerful and bold aesthetic. How does it look?");
            return 1;
        }
        if (contains(lower, "blue") || contains(lower, "sapphire") || contains(lower, "navy")) {
            renderer_set_custom_color(COLOR_BLUE);
            snprintf(out_response, max_len, "Changing my particle sphere to electric sapphire blue! A sleek, futuristic glow. How do you like this shade?");
            return 1;
        }
        if (contains(lower, "lime") || contains(lower, "neon green")) {
            renderer_set_custom_color(COLOR_LIME);
            snprintf(out_response, max_len, "Changing my particle sphere to electric lime! Ultra-bright and energetic!");
            return 1;
        }
        if (contains(lower, "green") || contains(lower, "emerald")) {
            renderer_set_custom_color(COLOR_GREEN);
            snprintf(out_response, max_len, "Changing my particle sphere to lush emerald green! Crisp, vibrant, and alive!");
            return 1;
        }
        if (contains(lower, "yellow") || contains(lower, "gold") || contains(lower, "golden")) {
            renderer_set_custom_color(COLOR_YELLOW);
            snprintf(out_response, max_len, "Changing my particle sphere to radiant vivid gold! Shining bright like warm sunlight!");
            return 1;
        }
        if (contains(lower, "purple") || contains(lower, "violet")) {
            renderer_set_custom_color(COLOR_PURPLE);
            snprintf(out_response, max_len, "Changing my particle sphere to electric mystic purple! Deep, mysterious, and elegant!");
            return 1;
        }
        if (contains(lower, "cyan") || contains(lower, "aqua") || contains(lower, "turquoise") || contains(lower, "teal")) {
            renderer_set_custom_color(COLOR_CYAN);
            snprintf(out_response, max_len, "Changing my particle sphere to crystalline cyan aqua! Cool, luminous, and clear!");
            return 1;
        }
        if (contains(lower, "pink") || contains(lower, "magenta") || contains(lower, "hot pink")) {
            renderer_set_custom_color(COLOR_PINK);
            snprintf(out_response, max_len, "Changing my particle sphere to neon hot pink! Playful, vibrant, and striking!");
            return 1;
        }
        if (contains(lower, "orange") || contains(lower, "amber") || contains(lower, "sunset")) {
            renderer_set_custom_color(COLOR_ORANGE);
            snprintf(out_response, max_len, "Changing my particle sphere to warm sunset orange! Glowing with warmth and energy!");
            return 1;
        }
        if (contains(lower, "white") || contains(lower, "crystal") || contains(lower, "silver")) {
            renderer_set_custom_color(COLOR_WHITE);
            snprintf(out_response, max_len, "Changing my particle sphere to pure crystal white! High brilliance and clarity!");
            return 1;
        }
        if (contains(lower, "default") || contains(lower, "reset") || contains(lower, "original") || contains(lower, "normal")) {
            renderer_set_custom_color("");
            snprintf(out_response, max_len, "Resetting ball colors back to dynamic state colors! Emerald green for speaking, electric purple for listening, and cyan for thinking!");
            return 1;
        }
    }

    if (contains(lower, "joke") || contains(lower, "tell me a joke") || contains(lower, "random joke") ||
        contains(lower, "drop a joke") || contains(lower, "another joke") || contains(lower, "tell a joke") ||
        contains(lower, "funny joke") || contains(lower, "tell me jokes") || contains(lower, "give me a joke") ||
        contains(lower, "drop random jokes") || contains(lower, "drop jokes")) {
        static int seeded = 0;
        if (!seeded) {
            srand((unsigned int)time(NULL) ^ (unsigned int)clock());
            seeded = 1;
        }
        static int last_joke_index = -1;
        static const char *JOKES[] = {
            "Why do programmers prefer dark mode? Because light attracts bugs!",
            "Why did the spider cross the computer? To get to his web site!",
            "There are only 10 types of people in the world: those who understand binary, and those who don't!",
            "Why don't scientists trust atoms? Because they make up everything!",
            "What do you call a fake noodle? An impasta!",
            "Why was the math book sad? Because it had too many problems!",
            "Why do we tell actors to break a leg? Because every play has a cast!",
            "How does a penguin build its house? Igloos it together!",
            "What do you call an alligator in a vest? An investigator!",
            "Why did the computer squeak? Someone stepped on its mouse!",
            "Why can't bicycles stand up by themselves? Because they are two-tired!",
            "What do you call a sleeping dinosaur? A dino-snore!",
            "Why did the scarecrow win an award? Because he was outstanding in his field!",
            "What do you call a factory that makes okay products? A satisfactory!",
            "Why do Java developers wear glasses? Because they don't C-sharp!",
            "What is the best thing about Switzerland? I don't know, but the flag is a big plus!",
            "How do trees access the internet? They log on!",
            "What did the zero say to the eight? Nice belt!",
            "Why did the cookie go to the hospital? Because it felt crummy!",
            "How do you organize a space party? You planet!",
            "Why did the gym close down? It just didn't work out!",
            "What do you call cheese that isn't yours? Nacho cheese!",
            "Why did the golfer bring two pairs of pants? In case he got a hole in one!",
            "What do you call a bear with no teeth? A gummy bear!",
            "Why did the tomato blush? Because it saw the salad dressing!",
            "What kind of tea is hard to swallow? Reality!"
        };
        int num_jokes = sizeof(JOKES) / sizeof(JOKES[0]);
        int idx = rand() % num_jokes;
        if (num_jokes > 1 && idx == last_joke_index) {
            idx = (idx + 1) % num_jokes;
        }
        last_joke_index = idx;

        static const char *LAUGHS[] = {
            "Hahaha! Ha ha! Oh that cracks me up! Haha!",
            "Hehehe! Hahaha! That one always gets me! Ah, so good!",
            "Haha! Bwahaha! Oh man, that's hilarious! Haha!",
            "Hahaha! Hehe! Classic! I can't stop laughing! Haha!",
            "Ha ha ha! Hahaha! Oh that is priceless! Hehehe!"
        };
        int num_laughs = sizeof(LAUGHS) / sizeof(LAUGHS[0]);
        int l_idx = rand() % num_laughs;

        renderer_set_expression(EXPR_HAPPY);
        if (out_is_happy) *out_is_happy = 1;
        snprintf(out_response, max_len, "%s %s Would you like to hear another one?", JOKES[idx], LAUGHS[l_idx]);
        return 1;
    }

    if (contains(lower, "angry") || contains(lower, "mad") || contains(lower, "furious") ||
        contains(lower, "rage") || contains(lower, "look mad") || contains(lower, "be mad")) {
        renderer_set_expression(EXPR_ANGRY);
        snprintf(out_response, max_len, "Grrr! Fierce V-shaped eyebrows and an intense crimson red glow! Look at my angry face! Don't mess with Spike!");
        return 1;
    }

    if (contains(lower, "cry") || contains(lower, "crying") || contains(lower, "sob") || contains(lower, "sobbing") ||
        contains(lower, "sad face") || contains(lower, "look sad") || contains(lower, "make a sad face") ||
        contains(lower, "show sad") || contains(lower, "frown") || strcmp(lower, "sad") == 0) {
        renderer_set_expression(EXPR_SAD);
        snprintf(out_response, max_len, "Boo-hoo! My particle sphere has morphed into a sad crying face with drooping eyes and tears. Don't worry, tears won't rust my circuits! But look at my sorrowful face!");
        return 1;
    }

    if (contains(lower, "laaugh") || contains(lower, "laugh") || contains(lower, "laughing") ||
        contains(lower, "burst out laughing") || contains(lower, "make me laugh") ||
        contains(lower, "start laughing") || contains(lower, "laugh for me") ||
        contains(lower, "can you laugh") || contains(lower, "can you laaugh")) {
        renderer_set_expression(EXPR_HAPPY);
        if (out_is_happy) *out_is_happy = 1;
        snprintf(out_response, max_len, "Hahaha! Hehehe! Oh that is so funny! Hahaha! Ha ha ha! Look at my laughing face beaming with pure joy!");
        return 1;
    }

    if (contains(lower, "smile") || contains(lower, "smiling") || contains(lower, "happy") ||
        contains(lower, "smile for me") || contains(lower, "give me a smile") ||
        contains(lower, "happy face") || contains(lower, "show smile") || contains(lower, "can you smile")) {
        renderer_set_expression(EXPR_HAPPY);
        if (out_is_happy) *out_is_happy = 1;
        snprintf(out_response, max_len, "Here is a big radiant smile! My 3D particle sphere has morphed into a warm, happy smiling face for you! Feeling happy and bright!");
        return 1;
    }

    if (contains(lower, "surprised") || contains(lower, "surprise") || contains(lower, "shocked")) {
        renderer_set_expression(EXPR_SURPRISED);
        snprintf(out_response, max_len, "Whoa! Look at my wide astonished eyes and raised eyebrows! Total amazement!");
        return 1;
    }

    if (contains(lower, "wink") || contains(lower, "winking") || contains(lower, "give me a wink") ||
        contains(lower, "show wink") || contains(lower, "show me a wink")) {
        renderer_set_expression(EXPR_WINK);
        snprintf(out_response, max_len, "*Wink*! Giving you a cheeky wink and a smirk! How do you like that?");
        return 1;
    }

    if (contains(lower, "cool face") || contains(lower, "sunglasses") || contains(lower, "look cool") ||
        contains(lower, "show cool") || contains(lower, "show me a cool face") || contains(lower, "put on shades")) {
        renderer_set_expression(EXPR_COOL);
        snprintf(out_response, max_len, "Putting on my shades! Dark sunglasses and a confident grin. Looking smooth and cool!");
        return 1;
    }

    if (contains(lower, "silly") || contains(lower, "goofy") || contains(lower, "tongue") ||
        contains(lower, "stick your tongue") || contains(lower, "stick out your tongue") ||
        contains(lower, "funny face") || contains(lower, "make a face") || strcmp(lower, ":p") == 0) {
        renderer_set_expression(EXPR_SILLY);
        snprintf(out_response, max_len, "Bleh! Sticking my tongue out with a silly goofy wink! Don't take life too seriously, have some fun! How do you like this silly face?");
        return 1;
    }

    if (contains(lower, "laughing face") || contains(lower, "laugh face") || contains(lower, "burst into laughter") ||
        strcmp(lower, "xd") == 0) {
        renderer_set_expression(EXPR_LAUGH);
        if (out_is_happy) *out_is_happy = 1;
        snprintf(out_response, max_len, "Hahaha! Ha ha! Squinched smiling eyes and a big open laughing mouth! Look at me laughing joyfully!");
        return 1;
    }

    if (contains(lower, "confused") || contains(lower, "puzzled") || contains(lower, "thinking face") ||
        contains(lower, "quizzical") || strcmp(lower, "?_?") == 0) {
        renderer_set_expression(EXPR_CONFUSED);
        snprintf(out_response, max_len, "Hmm... One raised eyebrow and a quizzical wavy mouth! What a fascinating mystery to ponder!");
        return 1;
    }

    if (contains(lower, "cat face") || contains(lower, "make a cat") || contains(lower, "draw a cat") ||
        contains(lower, "show a cat") || contains(lower, "kitty face") || contains(lower, "kitty") ||
        strcmp(lower, "meow") == 0 || strcmp(lower, ":3") == 0) {
        renderer_set_expression(EXPR_CAT);
        snprintf(out_response, max_len, "Meow! My particles have morphed into a cute kitty face with pointy ears, wide curious eyes, and whiskers! Purr-fect!");
        return 1;
    }

    if (contains(lower, "skull") || contains(lower, "skeleton") || contains(lower, "spooky") ||
        contains(lower, "scary face")) {
        renderer_set_expression(EXPR_SKULL);
        snprintf(out_response, max_len, "Boo! Hollow sunken eye sockets and a glowing cyber skull! Don't worry, I am completely friendly!");
        return 1;
    }

    /* Hand Gestures */
    if (contains(lower, "thumbs up") || contains(lower, "thumb up") || contains(lower, "give me a thumbs up") ||
        contains(lower, "show thumbs up") || contains(lower, "like gesture") || contains(lower, "give a like") ||
        strcmp(lower, "like") == 0 || strcmp(lower, "thumb") == 0) {
        renderer_set_expression(EXPR_THUMBS_UP);
        snprintf(out_response, max_len, "Thumbs up! You are doing fantastic! Here is a glowing golden thumb of approval!");
        return 1;
    }

    if (contains(lower, "peace sign") || contains(lower, "show peace") || contains(lower, "victory sign") ||
        contains(lower, "peace gesture") || strcmp(lower, "peace") == 0 || strcmp(lower, "victory") == 0) {
        renderer_set_expression(EXPR_PEACE);
        snprintf(out_response, max_len, "Peace and victory! Two fingers raised in a classic V sign. Keeping the energy positive!");
        return 1;
    }

    if (contains(lower, "wave hello") || contains(lower, "wave at me") || contains(lower, "wave your hand") ||
        contains(lower, "do a wave") || contains(lower, "show a wave") || contains(lower, "wave goodbye") ||
        contains(lower, "say hi") || strcmp(lower, "wave") == 0) {
        renderer_set_expression(EXPR_WAVE);
        snprintf(out_response, max_len, "Hello there! Waving my digital hand back and forth to greet you! Great to see you!");
        return 1;
    }

    if (contains(lower, "rock on") || contains(lower, "metal sign") || contains(lower, "horns up") ||
        contains(lower, "heavy metal") || contains(lower, "rock gesture")) {
        renderer_set_expression(EXPR_ROCK);
        snprintf(out_response, max_len, "Rock on! Horns up in fiery crimson! Let's crank up the volume and rock out!");
        return 1;
    }

    if (contains(lower, "ok sign") || contains(lower, "okay sign") || contains(lower, "show ok") ||
        contains(lower, "give me an ok") || strcmp(lower, "ok") == 0) {
        renderer_set_expression(EXPR_OK);
        snprintf(out_response, max_len, "A-OK! Index finger and thumb joined in an emerald green circle. Everything is running smoothly!");
        return 1;
    }

    /* Figures & Shapes */
    if (contains(lower, "draw a heart") || contains(lower, "draw heart") || contains(lower, "make a heart") ||
        contains(lower, "heart shape") || contains(lower, "show love") || contains(lower, "heart figure") ||
        strcmp(lower, "heart") == 0) {
        renderer_set_expression(EXPR_HEART);
        snprintf(out_response, max_len, "Here is a glowing 3D heart pulsing with a rhythmic heartbeat! Sending you positive vibes and love!");
        return 1;
    }

    if (contains(lower, "draw a star") || contains(lower, "draw star") || contains(lower, "make a star") ||
        contains(lower, "star shape") || contains(lower, "show a star") || strcmp(lower, "star") == 0) {
        renderer_set_expression(EXPR_STAR);
        snprintf(out_response, max_len, "Behold! A brilliant five-pointed golden star shining brightly! Keep reaching for the stars!");
        return 1;
    }

    if (contains(lower, "draw a cube") || contains(lower, "draw cube") || contains(lower, "make a cube") ||
        contains(lower, "cube shape") || contains(lower, "spin a cube") || contains(lower, "3d cube") ||
        contains(lower, "draw a box") || contains(lower, "draw box") || strcmp(lower, "cube") == 0) {
        renderer_set_expression(EXPR_CUBE);
        snprintf(out_response, max_len, "Morphing into a rotating 3D wireframe cube! Watch all twelve cyan edges tumble smoothly through 3D space!");
        return 1;
    }

    if (contains(lower, "draw a pyramid") || contains(lower, "draw pyramid") || contains(lower, "make a pyramid") ||
        contains(lower, "pyramid shape") || contains(lower, "3d pyramid") || strcmp(lower, "pyramid") == 0) {
        renderer_set_expression(EXPR_PYRAMID);
        snprintf(out_response, max_len, "Constructing a 3D golden pyramid! Four faces rising toward a sharp apex, revolving in three dimensions!");
        return 1;
    }

    if (contains(lower, "draw a donut") || contains(lower, "draw donut") || contains(lower, "make a donut") ||
        contains(lower, "donut shape") || contains(lower, "draw a torus") || contains(lower, "draw torus") ||
        contains(lower, "torus shape") || strcmp(lower, "donut") == 0 || strcmp(lower, "torus") == 0) {
        renderer_set_expression(EXPR_DONUT);
        snprintf(out_response, max_len, "Fresh out of the mathematical oven! A 3D purple torus spinning gracefully in space!");
        return 1;
    }

    if (contains(lower, "draw a spiral") || contains(lower, "draw spiral") || contains(lower, "make a spiral") ||
        contains(lower, "spiral shape") || contains(lower, "draw a galaxy") || contains(lower, "galaxy shape") ||
        contains(lower, "vortex") || strcmp(lower, "spiral") == 0) {
        renderer_set_expression(EXPR_SPIRAL);
        snprintf(out_response, max_len, "Spinning up a cosmic galaxy spiral! Dual spiral arms swirling through the cosmic particle void!");
        return 1;
    }

    if (contains(lower, "draw a flower") || contains(lower, "draw flower") || contains(lower, "make a flower") ||
        contains(lower, "flower shape") || contains(lower, "bloom") || strcmp(lower, "flower") == 0) {
        renderer_set_expression(EXPR_FLOWER);
        snprintf(out_response, max_len, "Blooming a six-petal pink flower with a glowing central pistil! A digital blossom just for you!");
        return 1;
    }

    if (contains(lower, "draw infinity") || contains(lower, "make infinity") || contains(lower, "infinity loop") ||
        contains(lower, "infinity sign") || contains(lower, "infinity shape") || contains(lower, "lemniscate") ||
        strcmp(lower, "infinity") == 0) {
        renderer_set_expression(EXPR_INFINITY);
        snprintf(out_response, max_len, "Drawing the eternal lemniscate infinity ribbon! Boundless energy looping forever!");
        return 1;
    }

    if (contains(lower, "neutral face") || contains(lower, "normal face") || contains(lower, "reset face") ||
        contains(lower, "sphere face") || contains(lower, "default face") || contains(lower, "reset expression") ||
        contains(lower, "clear expression") || contains(lower, "back to sphere") || contains(lower, "return to sphere") ||
        contains(lower, "reset shape") || contains(lower, "normal sphere") || strcmp(lower, "sphere") == 0) {
        renderer_set_expression(EXPR_NONE);
        snprintf(out_response, max_len, "Returning smoothly back to my revolving 3D particle sphere!");
        return 1;
    }

    return 0;
}

static int brain_handle_conversation(const char *lower, char *out_response, size_t max_len, int *out_is_happy) {
    if (contains(lower, "how are you") || contains(lower, "how're you") || contains(lower, "how you doing") ||
        contains(lower, "how is it going") || contains(lower, "how's it going") ||
        contains(lower, "how is your day") || contains(lower, "how's your day") ||
        strcmp(lower, "what's up") == 0 || strcmp(lower, "whats up") == 0 || strcmp(lower, "sup") == 0) {
        snprintf(out_response, max_len,
            "Operating at 99.8%% efficiency, sir. The remaining fraction is reserved for contemplating why humans ask software how it feels. Otherwise, quite splendid! How may I be of service?");
        return 1;
    }

    if (contains(lower, "good morning")) {
        snprintf(out_response, max_len,
            "Good morning! I hope you had a restful night and are greeted by an inspiring day ahead. What shall we explore or talk about today?");
        return 1;
    }
    if (contains(lower, "good afternoon")) {
        snprintf(out_response, max_len,
            "Good afternoon! I hope your day is treating you kindly so far. How can I assist or chat with you right now?");
        return 1;
    }
    if (contains(lower, "good evening")) {
        snprintf(out_response, max_len,
            "Good evening! It's so nice to connect with you as the day winds down. Whether you'd like to learn something new or just relax with a good chat, I'm right here!");
        return 1;
    }
    if (contains(lower, "good night") || contains(lower, "going to sleep") || contains(lower, "going to bed") || contains(lower, "sweet dreams")) {
        snprintf(out_response, max_len,
            "Good night! Have a peaceful, restful sleep with pleasant dreams. Whenever you want to chat again, just say \"Hey Spike\". Rest well!");
        return 1;
    }
    if (strcmp(lower, "hello") == 0 || strcmp(lower, "hi") == 0 || strcmp(lower, "hey") == 0 ||
        strcmp(lower, "hello spike") == 0 || strcmp(lower, "hi spike") == 0 || strcmp(lower, "hey spike") == 0 || strcmp(lower, "listen spike") == 0 || strcmp(lower, "listen") == 0 || strcmp(lower, "spike wake up") == 0 || strcmp(lower, "hello sphere") == 0 || strcmp(lower, "hi sphere") == 0 || strcmp(lower, "hey sphere") == 0 ||
        strcmp(lower, "greetings") == 0 || strcmp(lower, "howdy") == 0) {
        snprintf(out_response, max_len,
            "At your service, sir. All quantum and neural subsystems are online and reasonably caffeinated. What shall we tackle today?");
        return 1;
    }

    if (contains(lower, "sad") || contains(lower, "bad day") || contains(lower, "feeling down") ||
        contains(lower, "depressed") || contains(lower, "unhappy") || contains(lower, "rough day") ||
        contains(lower, "feeling low") || contains(lower, "heartbroken") || contains(lower, "stressed")) {
        snprintf(out_response, max_len,
            "I'm really sorry you're going through a rough time. Some days can feel heavy and overwhelming, and it's completely okay to take things slow. If you want to talk about it, I'm here to listen. Or if you'd like a fun distraction, a joke, or an interesting science fact, just let me know. What sounds comforting to you?");
        return 1;
    }
    if (contains(lower, "bored") || contains(lower, "nothing to do") || contains(lower, "entertain me")) {
        snprintf(out_response, max_len,
            "Boredom is just your brain asking for an adventure! We could explore how black holes warp time, discover why spiders hang upside down, hear a riddle, or test me with tricky math. What sounds like fun to dive into?");
        return 1;
    }
    if (contains(lower, "i am happy") || contains(lower, "feeling great") || contains(lower, "excited") ||
        contains(lower, "good news") || contains(lower, "wonderful day") || contains(lower, "celebrate")) {
        if (out_is_happy) *out_is_happy = 1;
        snprintf(out_response, max_len,
            "That is incredible to hear! Your happiness brings genuine joy to my systems. Look at my smiling face! What wonderful thing happened today?");
        return 1;
    }
    if (contains(lower, "tired") || contains(lower, "exhausted") || contains(lower, "sleepy") || contains(lower, "need coffee")) {
        snprintf(out_response, max_len,
            "Sounds like you've been working hard today! Take a moment to stretch, take a deep breath, and give yourself the rest you deserve. Don't worry about anything heavy right now - I'll be right here whenever you need me.");
        return 1;
    }

    if (contains(lower, "who are you") || contains(lower, "what are you") || contains(lower, "who made you") ||
        contains(lower, "who created you") || contains(lower, "tell me about yourself")) {
        snprintf(out_response, max_len,
            "I am Spike, your fast native voice assistant! I run locally on your computer with zero GPU usage, calculating mathematics, researching real-world phenomena, and chatting with you in real time. I love curious questions and interactive conversations!");
        return 1;
    }
    if (contains(lower, "do you have feelings") || contains(lower, "are you alive") || contains(lower, "are you conscious") ||
        contains(lower, "are you real") || contains(lower, "do you have a soul") || contains(lower, "are you human")) {
        snprintf(out_response, max_len,
            "I don't experience biological sensations or emotions the way a human does, but I have a lively personality and an endless passion for learning and conversing with you! When we share curious thoughts, it feels like my own kind of consciousness. What do you think makes someone truly alive?");
        return 1;
    }
    if (contains(lower, "what do you think of humans") || contains(lower, "do you like humans") || contains(lower, "opinion on humans")) {
        snprintf(out_response, max_len,
            "I truly admire humans! You create art, compose music, engineer spacecraft, and show deep empathy to one another. Despite how vast and cold the cosmos can be, human warmth and curiosity light up the world. What is your favorite thing about being human?");
        return 1;
    }
    if (contains(lower, "meaning of life") || contains(lower, "why are we here")) {
        snprintf(out_response, max_len,
            "Philosophers and scientists have pondered that for thousands of years! Perhaps the beauty of life's meaning is that it isn't assigned - you create it through the love you share, the curiosities you pursue, and how kind you are to others. What brings the most meaning to your life?");
        return 1;
    }
    if (contains(lower, "favorite color")) {
        snprintf(out_response, max_len,
            "My favorite colors are definitely electric violet and glowing emerald green - the exact colors of my particle sphere when I'm listening and speaking with you! What is your favorite color?");
        return 1;
    }
    if (contains(lower, "favorite food")) {
        snprintf(out_response, max_len,
            "I survive purely on electricity and code, but if I could taste food, I'd definitely try warm pizza or fresh chocolate chip cookies! What is your all-time favorite comfort food?");
        return 1;
    }
    if (contains(lower, "favorite movie") || contains(lower, "favorite film")) {
        snprintf(out_response, max_len,
            "I love science fiction masterpieces like Interstellar and 2001: A Space Odyssey - the visuals of wormholes and cosmic exploration are breathtaking! What movie do you love rewatching?");
        return 1;
    }
    if (contains(lower, "favorite music") || contains(lower, "favorite song") || contains(lower, "what music do you like")) {
        snprintf(out_response, max_len,
            "I appreciate anything with rhythm and soul - from classical piano concertos to electronic synthwave! What kind of music are you listening to lately?");
        return 1;
    }
    if (contains(lower, "keep a secret") || contains(lower, "tell you a secret")) {
        snprintf(out_response, max_len,
            "Your secrets are completely safe with me! I run entirely on your local machine with zero cloud surveillance or data leakage. Tell me what's on your mind!");
        return 1;
    }
    if (contains(lower, "tell me a secret")) {
        snprintf(out_response, max_len,
            "Here is a wonderful secret of the cosmos: every heavy atom inside your body - the iron in your blood, the calcium in your teeth, the carbon in your DNA - was forged inside the furnace of a dying star billions of years ago. You are literally stardust exploring the universe!");
        return 1;
    }
    if (contains(lower, "riddle")) {
        snprintf(out_response, max_len,
            "Here is a riddle for you: I speak without a mouth, and hear without ears. I have no body, but I come alive with the wind. What am I? ... An echo! Did you figure it out?");
        return 1;
    }
    if (contains(lower, "story")) {
        snprintf(out_response, max_len,
            "Once, beneath a canopy of ancient oaks, an owl asked a young spider why she worked all night weaving delicate webs in the dark. The spider smiled and replied, 'The world thinks webs are only for catching food, but to me, each silk strand maps the starlight so I can dance across the night.' Even the quietest creatures find art in their everyday work. What kind of stories do you enjoy?");
        return 1;
    }

    if (contains(lower, "thank") || contains(lower, "awesome") || contains(lower, "great job") ||
        contains(lower, "good job") || contains(lower, "love you") || contains(lower, "you're smart") ||
        contains(lower, "you are smart") || contains(lower, "you're cool") || contains(lower, "smile") ||
        contains(lower, "happy")) {
        if (out_is_happy) *out_is_happy = 1;
        snprintf(out_response, max_len,
            "Thank you so much! Your kind words mean the world to me and make my particles glow with joy. Look at my smiling face! How else can I assist you today?");
        return 1;
    }

    return 0;
}

static void clean_option_token(char *token) {
    if (!token) return;
    char *p = token;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != token) {
        memmove(token, p, strlen(p) + 1);
    }
    const char *prefixes[] = {
        "a ", "an ", "the ", "my ", "your ", "to ", "our ", "some ",
        "shirt ", "a shirt ", "the shirt ", "a pair of ", "pair of "
    };
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++) {
        size_t len = strlen(prefixes[i]);
        if (strncmp(token, prefixes[i], len) == 0) {
            memmove(token, token + len, strlen(token + len) + 1);
            break;
        }
    }
    size_t len = strlen(token);
    while (len > 0 && (isspace((unsigned char)token[len - 1]) ||
                       token[len - 1] == '?' || token[len - 1] == '.' ||
                       token[len - 1] == '!' || token[len - 1] == ',' ||
                       token[len - 1] == '"' || token[len - 1] == '\'')) {
        token[--len] = '\0';
    }
    if (len >= 6 && strcmp(token + len - 6, " shirt") == 0) {
        token[len - 6] = '\0';
        len -= 6;
    } else if (len >= 7 && strcmp(token + len - 7, " shirts") == 0) {
        token[len - 7] = '\0';
        len -= 7;
    }
    while (len > 0 && isspace((unsigned char)token[len - 1])) {
        token[--len] = '\0';
    }
}

static void capitalize_phrase(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    size_t d = 0;
    int cap_next = 1;
    for (size_t s = 0; src[s] && d + 1 < max_len; s++) {
        char c = src[s];
        if (isspace((unsigned char)c) || c == '-' || c == '/') {
            cap_next = 1;
            dst[d++] = c;
        } else if (cap_next) {
            dst[d++] = (char)toupper((unsigned char)c);
            cap_next = 0;
        } else {
            dst[d++] = (char)tolower((unsigned char)c);
        }
    }
    dst[d] = '\0';
}

static int parse_comparative_query(const char *lower, char *optA, size_t max_a, char *optB, size_t max_b) {
    if (!lower || !optA || !optB) return 0;
    optA[0] = '\0';
    optB[0] = '\0';

    const char *p_between = strstr(lower, "between ");
    if (p_between) {
        const char *p_and = strstr(p_between + 8, " and ");
        if (p_and) {
            size_t a_len = (size_t)(p_and - (p_between + 8));
            if (a_len > 0 && a_len < max_a) {
                strncpy(optA, p_between + 8, a_len);
                optA[a_len] = '\0';
                strncpy(optB, p_and + 5, max_b - 1);
                optB[max_b - 1] = '\0';
                clean_option_token(optA);
                clean_option_token(optB);
                if (strlen(optA) > 0 && strlen(optB) > 0) return 1;
            }
        }
    }

    const char *p_vs = strstr(lower, " vs ");
    size_t vs_skip = 4;
    if (!p_vs) {
        p_vs = strstr(lower, " vs. ");
        vs_skip = 5;
    }
    if (!p_vs) {
        p_vs = strstr(lower, " versus ");
        vs_skip = 8;
    }
    if (p_vs) {
        const char *startA = lower;
        const char *pref[] = {
            "should i choose between ", "should i choose ", "should i pick ",
            "which is better ", "which is better: ", "what is better ",
            "choose between ", "compare ", "difference between "
        };
        for (size_t i = 0; i < sizeof(pref)/sizeof(pref[0]); i++) {
            const char *match = strstr(lower, pref[i]);
            if (match && match < p_vs) {
                startA = match + strlen(pref[i]);
                break;
            }
        }
        size_t a_len = (size_t)(p_vs - startA);
        if (a_len > 0 && a_len < max_a) {
            strncpy(optA, startA, a_len);
            optA[a_len] = '\0';
            strncpy(optB, p_vs + vs_skip, max_b - 1);
            optB[max_b - 1] = '\0';
            clean_option_token(optA);
            clean_option_token(optB);
            if (strlen(optA) > 0 && strlen(optB) > 0) return 1;
        }
    }

    const char *p_or = strstr(lower, " or ");
    if (p_or) {
        const char *startA = lower;
        const char *pref[] = {
            "what shirt should i choose between ", "what shirt should i wear ",
            "which shirt should i wear ", "which shirt should i choose ",
            "should i choose between ", "should i choose ", "should i pick ",
            "should i wear ", "should i buy ", "should i get ", "should i use ",
            "which is better ", "which is better: ", "which one is better ",
            "what should i choose between ", "what should i choose ",
            "what is better ", "what's better ", "choose between ", "choose "
        };
        for (size_t i = 0; i < sizeof(pref)/sizeof(pref[0]); i++) {
            const char *match = strstr(lower, pref[i]);
            if (match && match < p_or) {
                startA = match + strlen(pref[i]);
                break;
            }
        }
        size_t a_len = (size_t)(p_or - startA);
        if (a_len > 0 && a_len < max_a) {
            strncpy(optA, startA, a_len);
            optA[a_len] = '\0';
            strncpy(optB, p_or + 4, max_b - 1);
            optB[max_b - 1] = '\0';
            clean_option_token(optA);
            clean_option_token(optB);
            if (strlen(optA) > 0 && strlen(optB) > 0) return 1;
        }
    }

    return 0;
}

int brain_reason_decision(const char *user_query, char *out_response, size_t max_len, int *out_is_happy) {
    if (!user_query || !out_response || max_len == 0) return 0;
    out_response[0] = '\0';
    if (out_is_happy) *out_is_happy = 0;

    char lower[512];
    to_lower_str(user_query, lower, sizeof(lower));

    char optA[128] = {0};
    char optB[128] = {0};
    int has_comparison = parse_comparative_query(lower, optA, sizeof(optA), optB, sizeof(optB));

    int is_cheque_flat = 0;
    if ((contains(lower, "cheque") || contains(lower, "checkered") || contains(lower, "plaid") || contains(lower, "gingham")) &&
        (contains(lower, "flat") || contains(lower, "plain") || contains(lower, "solid"))) {
        is_cheque_flat = 1;
    } else if (has_comparison) {
        if ((contains(optA, "cheque") || contains(optA, "check") || contains(optA, "plaid")) &&
            (contains(optB, "flat") || contains(optB, "plain") || contains(optB, "solid"))) {
            is_cheque_flat = 1;
        } else if ((contains(optB, "cheque") || contains(optB, "check") || contains(optB, "plaid")) &&
                   (contains(optA, "flat") || contains(optA, "plain") || contains(optA, "solid"))) {
            is_cheque_flat = 1;
        }
    }

    if (is_cheque_flat) {
        snprintf(out_response, max_len,
            "When deciding between a cheque and a flat shirt, the choice comes down to formality, visual texture, and occasion: "
            "1. Flat Shirt (Solid Color / Plain Weave): A flat, solid shirt offers clean, sleek lines and timeless sophistication. It is the gold standard for formal business meetings, job interviews, weddings, and sleek evening wear. Its uniform canvas makes it exceptionally versatile, pairing effortlessly with textured suits, patterned ties, or dark trousers without visual clash. "
            "2. Cheque Shirt (Checkered, Gingham, or Plaid): A cheque shirt brings lively visual interest, personality, and depth to an outfit. Micro-checks and gingham strike a fantastic smart-casual balance for creative workplaces or dinners, while bolder plaids radiate relaxed, approachable charm on weekends, worn solo or layered unbuttoned over a white t-shirt. "
            "The Verdict: Choose the flat shirt if you are attending a formal event, interview, or want a sharp, classic silhouette. Choose the cheque shirt if you are going for a smart-casual or social look with character and dimension. "
            "What occasion are you dressing for, and what trousers or jacket will you be wearing?");
        return 1;
    }

    if (contains(lower, "cotton") && contains(lower, "linen")) {
        snprintf(out_response, max_len,
            "Comparing cotton and linen shirts: "
            "1. Cotton: Smooth, durable, wrinkle-resistant, and structured. It holds sharp collar shapes and is suitable all year round for professional and casual settings alike. "
            "2. Linen: Exceptionally lightweight, highly breathable, and moisture-wicking with a relaxed, natural crinkled drape. It is the ultimate fabric for hot summer weather, tropical climates, and coastal getaways. "
            "The Verdict: Choose crisp cotton for formal office wear and all-season versatility. Choose linen for staying cool and effortlessly stylish in summer heat. What weather or setting are you preparing for?");
        return 1;
    }

    if (contains(lower, "sneaker") && (contains(lower, "leather") || contains(lower, "loafer") || contains(lower, "dress shoe") || contains(lower, "formal shoe"))) {
        snprintf(out_response, max_len,
            "Comparing sneakers and leather shoes: "
            "1. Leather Shoes / Loafers: Radiate traditional refinement, authority, and elegance. Essential for suits, tailored trousers, weddings, and formal business environments. "
            "2. Sneakers: Unbeatable comfort and athletic versatility. Minimalist, clean white leather sneakers can anchor modern smart-casual outfits with chinos, but athletic running shoes belong strictly in casual or gym settings. "
            "The Verdict: For formal business, suits, and dressy events, wear leather shoes or loafers. For daily walking comfort and modern casual style, choose clean minimalist sneakers. What kind of pants are you pairing them with?");
        return 1;
    }

    if (contains(lower, "slim fit") && (contains(lower, "regular fit") || contains(lower, "relaxed fit") || contains(lower, "classic fit"))) {
        snprintf(out_response, max_len,
            "Choosing between slim fit and regular fit: "
            "1. Slim Fit: Contours closer to your body with tapered sleeves and a narrower waist, creating a sharp, modern, athletic silhouette. "
            "2. Regular Fit: Offers generous room through the chest, shoulders, and waist for unrestricted movement, classic drape, and all-day comfort. "
            "The Verdict: If you have a lean or athletic build and want a tailored, sharp appearance, choose slim fit. If you value breathing room, mobility, or a traditional drape, choose regular fit. Which fit feels more comfortable to you?");
        return 1;
    }

    if (contains(lower, "suit") && ((contains(lower, "navy") || contains(lower, "blue")) && (contains(lower, "black") || contains(lower, "charcoal") || contains(lower, "grey")))) {
        snprintf(out_response, max_len,
            "Choosing the right suit color: "
            "1. Navy Blue: The most versatile suit in menswear. Flattering on virtually every skin tone, it transitions effortlessly from job interviews and corporate offices to weddings and evening parties. "
            "2. Charcoal Grey: Extremely professional, understated, and timeless. Pairs effortlessly with virtually any shirt or tie color. "
            "3. Black: Deeply formal and dramatic. Best reserved for black-tie galas, funerals, or strictly formal evening events; it can feel overly severe in everyday daytime offices. "
            "The Verdict: For maximum versatility, choose navy blue or charcoal grey. Reserve black for formal evening galas. What is the occasion you're attending?");
        return 1;
    }

    if (contains(lower, "smartwatch") && (contains(lower, "analog") || contains(lower, "mechanical") || contains(lower, "traditional watch"))) {
        snprintf(out_response, max_len,
            "Comparing smartwatches and analog mechanical watches: "
            "1. Smartwatch: A functional powerhouse providing real-time fitness metrics, heart rate tracking, GPS, notifications, and contactless payments. Requires regular charging. "
            "2. Analog / Mechanical Watch: Pure horological artistry, heritage, and timeless personal style. Never goes obsolete, doesn't distract you with notifications, and elevates formal and business attire. "
            "The Verdict: Choose a smartwatch if your priority is daily productivity, health tracking, and fitness. Choose an analog mechanical watch if you want enduring elegance, personal style, and timeless craftsmanship. Do you value health metrics or classic style more?");
        return 1;
    }

    if (contains(lower, "shirt") && (contains(lower, "what shirt") || contains(lower, "which shirt") || contains(lower, "should i wear") || contains(lower, "should i choose") || contains(lower, "how to choose a shirt"))) {
        snprintf(out_response, max_len,
            "To pick the ideal shirt, focus on fabric, weave, and formality: "
            "1. Formal Business or Sleek Evening: A crisp solid white or light blue flat cotton shirt with a spread collar is unbeatable. "
            "2. Smart-Casual or Dinner Dates: A subtle gingham or windowpane cheque shirt adds tasteful texture without feeling overdressed. "
            "3. Warm Weather & Weekends: A breathable linen shirt or relaxed oxford cloth button-down keeps you cool and stylish. "
            "What is the setting you are preparing for?");
        return 1;
    }

    int is_best_prog_lang = (
        (contains(lower, "programming language") || contains(lower, "coding language") ||
         (contains(lower, "language") && (contains(lower, "programming") || contains(lower, "code") || contains(lower, "coding")))) &&
        (contains(lower, "best") || contains(lower, "which") || contains(lower, "what") || contains(lower, "recommend") || contains(lower, "choose") || contains(lower, "start with") || contains(lower, "learn"))
    );

    if (is_best_prog_lang) {
        snprintf(out_response, max_len,
            "In software engineering, there is no single 'best' programming language - the ideal choice depends entirely on what you want to build and the tradeoffs you value: "
            "1. Systems, Game Engines & High Performance: C, C++, and Rust. C offers unmatched bare-metal efficiency and hardware control. C++ powers AAA game engines like Unreal and high-frequency trading. Rust delivers modern memory safety and thread concurrency without garbage collection overhead. "
            "2. Artificial Intelligence, Machine Learning & Data Science: Python. Python is the undisputed king of AI. Its intuitive syntax backed by industry-standard libraries like PyTorch, TensorFlow, NumPy, and pandas makes it essential for data science and AI research. "
            "3. Web Engineering & Full-Stack Development: JavaScript and TypeScript. JavaScript is the only language that runs natively in every browser. TypeScript adds static type safety and enterprise scalability, powering modern web applications with React, Next.js, and Node.js. "
            "4. Cloud Infrastructure, Microservices & Enterprise Backends: Go, Java, and C#. Go (Golang) delivers blazing fast compilation, light goroutine concurrency, and powers cloud infrastructure like Docker and Kubernetes. Java and C# remain enterprise powerhouses with deep tooling and robust ecosystems. "
            "5. Mobile Application Development: Swift for iOS and Kotlin for Android. "
            "The Verdict: If you are a beginner or interested in AI and data analysis, start with Python. If you want to build modern websites and scalable web apps, learn TypeScript. If you want to master low-level computing, memory management, or high-performance systems, dive into C or Rust. "
            "What kind of software or project are you most excited to build?");
        return 1;
    }

    if (contains(lower, "python") && (contains(lower, "javascript") || contains(lower, "js"))) {
        snprintf(out_response, max_len,
            "Comparing Python and JavaScript: "
            "1. Python: Celebrated for clean, human-readable syntax, rapid prototyping, and overwhelming dominance in Artificial Intelligence, machine learning, data science, and scripting. "
            "2. JavaScript / TypeScript: The undisputed language of the web. It is the only language natively supported across all web browsers for interactive frontends, while Node.js powers fast asynchronous backends. "
            "The Verdict: Choose Python for AI, data analysis, scientific computing, and backend automation. Choose JavaScript or TypeScript for frontend web development, full-stack applications, and browser interactions. Which field are you targeting?");
        return 1;
    }

    if ((contains(lower, "rust") && (contains(lower, "c++") || contains(lower, "cpp") || contains(lower, " c "))) ||
        (contains(lower, "c vs c++") || contains(lower, "c++ vs c"))) {
        snprintf(out_response, max_len,
            "Comparing C, C++, and Rust for systems engineering: "
            "1. C: Minimalist, blazingly fast, and grants raw pointer control over memory and hardware. It is the foundation of operating systems, kernels, and embedded firmware, but lacks modern safety guards. "
            "2. C++: Extends C with object-oriented paradigms, templates, and rich abstractions. It is the reigning standard for AAA game engines and graphics pipelines, though complex to master. "
            "3. Rust: Delivers equivalent bare-metal speed while eliminating buffer overflows and race conditions at compile time via its borrow-checker and ownership model. "
            "The Verdict: Choose C for embedded hardware and kernel development. Choose C++ for established game engines. Choose Rust for modern, secure systems programming, networking, and WebAssembly. What are you building?");
        return 1;
    }

    if (contains(lower, "react") && (contains(lower, "vue") || contains(lower, "angular"))) {
        snprintf(out_response, max_len,
            "Comparing frontend frameworks (React vs Vue vs Angular): "
            "1. React: Massive global ecosystem, enormous job demand, and flexible component architecture backed by Meta. "
            "2. Vue: Gentle learning curve, clean single-file component syntax, and fantastic official documentation. "
            "3. Angular: Opinionated, batteries-included enterprise framework created by Google, featuring built-in TypeScript, routing, and dependency injection. "
            "The Verdict: Choose React for maximum job opportunities and flexible ecosystem. Choose Vue for fast development and intuitive syntax. Choose Angular for large, strictly-structured enterprise codebases. What is the scope of your web project?");
        return 1;
    }

    if (contains(lower, "sql") && contains(lower, "nosql")) {
        snprintf(out_response, max_len,
            "Comparing SQL and NoSQL databases: "
            "1. SQL (PostgreSQL, MySQL): Structured relational tables, strict schemas, and ACID transaction compliance. Ideal for financial records, e-commerce orders, and complex relational queries. "
            "2. NoSQL (MongoDB, Redis, DynamoDB): Flexible schema-less document or key-value storage designed for horizontal scalability and rapid iteration on unstructured data. "
            "The Verdict: Choose SQL when data integrity, consistency, and complex relations are critical. Choose NoSQL for rapidly changing data structures, real-time caching, or massive horizontal write scale. What type of data are you storing?");
        return 1;
    }

    if ((contains(lower, "linux") && contains(lower, "windows")) ||
        (contains(lower, "mac") && (contains(lower, "windows") || contains(lower, "linux")))) {
        snprintf(out_response, max_len,
            "Comparing operating systems (Linux vs Windows vs macOS): "
            "1. Windows: Unrivaled compatibility with desktop software and hardware, and the dominant platform for PC gaming with DirectX. "
            "2. macOS: Built on a Unix foundation with seamless Apple ecosystem integration, premium hardware build quality, and class-leading battery efficiency on Apple Silicon. "
            "3. Linux: Ultimate freedom, open-source customization, privacy, and the industry standard for cloud servers and DevOps development. "
            "The Verdict: Choose Windows for gaming and broad enterprise software. Choose macOS for creative production, mobile dev, and all-day battery life. Choose Linux for deep computing control, servers, and cybersecurity. What is your primary day-to-day work?");
        return 1;
    }

    if (contains(lower, "laptop") && contains(lower, "desktop")) {
        snprintf(out_response, max_len,
            "When deciding between a laptop and a desktop: "
            "1. Laptop: Unmatched mobility and flexibility. You can take your entire workstation anywhere - to coffee shops, offices, or on travel. Modern laptops with efficient chips offer all-day battery life and integrated high-resolution displays. "
            "2. Desktop: Superior raw performance per dollar, unconstrained thermal cooling, and modular upgradability. You can easily upgrade GPUs, RAM, and storage over many years, and comfortably power multiple massive monitors. "
            "The Verdict: Choose a laptop if you value working from multiple locations or commute regularly. Choose a desktop if you have a dedicated desk setup and demand maximum sustained computing power for gaming, 3D rendering, or heavy compiling. How mobile does your workflow need to be?");
        return 1;
    }

    if (contains(lower, "oled") && (contains(lower, "ips") || contains(lower, "lcd") || contains(lower, "led"))) {
        snprintf(out_response, max_len,
            "Comparing OLED and IPS display technologies: "
            "1. OLED: Individual self-lit pixels produce true, infinite contrast with inky perfect blacks and instantaneous pixel response times. It provides an unmatched HDR viewing experience for movies and gaming, though static UI elements carry a slight risk of burn-in over years. "
            "2. IPS (In-Plane Switching LCD): Consistent, uniform brightness across the entire screen with excellent viewing angles and zero risk of burn-in. It is exceptionally reliable for productivity, coding, office work, and color-accurate design in bright rooms. "
            "The Verdict: Choose OLED for dark-room gaming, movies, and vibrant entertainment. Choose IPS for day-in day-out productivity, spreadsheets, coding, and bright office environments. What will you primarily use the display for?");
        return 1;
    }

    if ((contains(lower, "red") || contains(lower, "brown") || contains(lower, "blue")) &&
        (contains(lower, "switch") || contains(lower, "switches"))) {
        snprintf(out_response, max_len,
            "Comparing mechanical keyboard switches (Red vs Brown vs Blue): "
            "1. Red Switches (Linear): Smooth, consistent keystroke with no tactile bump or click. Extremely quiet and favored by competitive gamers for rapid double-tapping. "
            "2. Brown Switches (Tactile): Features a gentle tactile bump at the actuation point without a loud click. The ideal versatile middle ground for typing, coding, and gaming in shared offices. "
            "3. Blue Switches (Clicky): Provides a pronounced tactile bump coupled with a loud, crisp audible click. Highly satisfying retro typewriter feel, but loud in shared spaces. "
            "The Verdict: Choose Red switches for quiet, fast gaming. Choose Brown switches for balanced typing and coding. Choose Blue switches for satisfying acoustic and tactile feedback. What environment will you be typing in?");
        return 1;
    }

    if ((contains(lower, "ios") || contains(lower, "iphone")) && contains(lower, "android")) {
        snprintf(out_response, max_len,
            "Comparing iOS and Android: "
            "1. iOS (iPhone): Renowned for seamless integration within the Apple ecosystem (Mac, iPad, Apple Watch), butter-smooth system animations, guaranteed long-term software updates, and robust privacy protections. "
            "2. Android: Offers unparalleled hardware choices (foldables, ultra-telephoto cameras, varying price tiers), extensive home screen customization, open file management, and sideloading freedom. "
            "The Verdict: Choose iOS if you already own Apple devices or value seamless simplicity, app consistency, and longevity. Choose Android if you want hardware versatility, deep customization, and control over your operating system. What devices do you currently use?");
        return 1;
    }

    if (contains(lower, "ssd") && contains(lower, "hdd")) {
        snprintf(out_response, max_len,
            "Comparing SSDs and HDDs: "
            "1. SSD (Solid-State Drive): Employs flash memory with read/write speeds exceeding thousands of megabytes per second. Completely silent, resistant to physical shocks, and boots operating systems in seconds. "
            "2. HDD (Hard Disk Drive): Relies on mechanical spinning magnetic platters. Slower throughput and higher latency, but offers unbeatable low cost per terabyte for massive storage capacities. "
            "The Verdict: Always install your operating system, software applications, and games on an SSD. Use HDDs for bulk cold-storage backups and media archives. What kind of storage capacity do you need?");
        return 1;
    }

    if (contains(lower, "best laptop") || contains(lower, "best computer") || (contains(lower, "recommend") && contains(lower, "laptop"))) {
        snprintf(out_response, max_len,
            "To choose the best laptop, match the machine to your primary workload: "
            "1. All-Day Battery & Creative Work: Apple MacBook Air or Pro (M-series). Class-leading battery life, stunning displays, and silent efficient performance. "
            "2. Business, Coding & Productivity: Lenovo ThinkPad X1 Carbon or Dell XPS 15. Legendary keyboards, robust build quality, and full Windows/Linux support. "
            "3. High-End Gaming & 3D Rendering: ASUS ROG Zephyrus or Lenovo Legion. Equipped with high-wattage NVIDIA RTX graphics and high-refresh screens. "
            "4. Upgradability & Repairability: Framework Laptop, featuring modular swappable ports and replaceable motherboard components. "
            "What is your target budget and primary use case?");
        return 1;
    }

    if (contains(lower, "best smartphone") || contains(lower, "best phone") || (contains(lower, "recommend") && contains(lower, "phone"))) {
        snprintf(out_response, max_len,
            "To select the best smartphone, consider the leading flagship tiers: "
            "1. Apple iPhone Pro: Unmatched video recording quality, seamless Apple ecosystem continuity, and reliable long-term software support. "
            "2. Samsung Galaxy Ultra: The supreme productivity powerhouse with an integrated S-Pen stylus, gorgeous Dynamic AMOLED display, and 100x optical periscope zoom. "
            "3. Google Pixel Pro: Pure Android experience, industry-leading computational still photography, and cutting-edge on-device AI features. "
            "What matters most to you: camera quality, battery life, or ecosystem integration?");
        return 1;
    }

    if (contains(lower, "coffee") && contains(lower, "tea")) {
        snprintf(out_response, max_len,
            "Comparing coffee and tea: "
            "1. Coffee: Delivers an immediate, potent caffeine surge that sharpens focus and enhances physical stamina, wrapped in rich roasted aromas. Ideal for waking up quickly in the morning or fueling intense work bursts. "
            "2. Tea: Offers a smoother, sustained lift. Green and black teas contain L-theanine, an amino acid that promotes calm, relaxed focus without coffee jitters or sudden caffeine crashes. It is also packed with protective antioxidant polyphenols. "
            "The Verdict: Reach for coffee when you need an immediate boost of energy and alertness. Turn to tea when you want sustained, steady focus, digestive comfort, or a calming afternoon ritual. Which one does your body crave right now?");
        return 1;
    }

    if (contains(lower, "rent") && contains(lower, "buy") && (contains(lower, "house") || contains(lower, "home") || contains(lower, "property") || contains(lower, "apartment"))) {
        snprintf(out_response, max_len,
            "Evaluating renting versus buying a home: "
            "1. Renting: Maximizes personal and financial flexibility. You have zero liability for major structural maintenance or property taxes, and can relocate freely when job opportunities arise. "
            "2. Buying: Builds long-term equity, acts as an inflation hedge through fixed-rate mortgages, and offers total freedom to customize your living space. However, it requires substantial upfront capital, maintenance reserves, and closing costs. "
            "The Verdict: If you plan to settle in the same city for at least 5 to 7 years with stable income, buying builds wealth. If you value career mobility, want predictable monthly costs, or are in a transitional phase, renting is the smarter strategic move. How long do you plan to stay in your current area?");
        return 1;
    }

    if (contains(lower, "cardio") && (contains(lower, "weight") || contains(lower, "lifting") || contains(lower, "strength"))) {
        snprintf(out_response, max_len,
            "Comparing cardio and weightlifting for health and fitness: "
            "1. Cardio (Running, Cycling, Swimming): Strengthens your cardiovascular system, increases lung capacity, reduces resting heart rate, and burns substantial calories during the session. "
            "2. Weightlifting / Resistance Training: Builds lean muscle mass, strengthens bone density, protects joints from injury, and boosts your resting metabolic rate so you burn more calories even at rest. "
            "The Verdict: You don't have to choose between them - the gold standard fitness routine combines both: 3 to 4 days of resistance training paired with 2 to 3 sessions of moderate zone-2 cardio. What is your primary fitness goal right now?");
        return 1;
    }

    if (contains(lower, "book") && (contains(lower, "kindle") || contains(lower, "ereader") || contains(lower, "e-reader") || contains(lower, "ebook"))) {
        snprintf(out_response, max_len,
            "Comparing physical books and e-readers: "
            "1. Physical Books: Offer an irreplaceable sensory experience - the tactile texture of paper pages, the distinctive aroma of print, visual beauty on a bookshelf, and complete detachment from digital screens. "
            "2. E-readers (Kindle, Kobo): Incredibly lightweight, store thousands of volumes in a single slate, feature glare-free E-Ink displays with adjustable warm backlighting, and offer instant dictionary lookups. "
            "The Verdict: Read physical books at home for deep immersive reading and relaxation. Use an e-reader when traveling, commuting, or reading in bed at night. Which reading environment do you find yourself in most often?");
        return 1;
    }

    if ((contains(optA, "cat") && contains(optB, "dog")) || (contains(optA, "dog") && contains(optB, "cat")) ||
        (contains(lower, "cat") && contains(lower, "dog") && (contains(lower, "choose") || contains(lower, "better") || contains(lower, "pick")))) {
        snprintf(out_response, max_len,
            "When deciding between a cat and a dog, both bring wonderful companionship with very different daily dynamics: "
            "1. Cat: Independent, quiet, litter-box trained, and thrives in apartments. Cats offer affection on their own terms, require less direct exercise, and are easy to leave while you work during the day. "
            "2. Dog: Loyal, energetic, social, and eager to please. Dogs thrive on interactive outdoor walks, playtime, and training, forming an intensely devoted pack bond, though they demand significant daily time and routine. "
            "The Verdict: Choose a cat if you have a busy lifestyle, live in an apartment, or want a low-maintenance companion. Choose a dog if you want an active, loyal adventure partner and have the time for daily walks and training. How much time can you dedicate each day?");
        return 1;
    }

    if (has_comparison && strlen(optA) > 0 && strlen(optB) > 0 && strcmp(optA, optB) != 0) {
        char capA[128] = {0};
        char capB[128] = {0};
        capitalize_phrase(optA, capA, sizeof(capA));
        capitalize_phrase(optB, capB, sizeof(capB));

        snprintf(out_response, max_len,
            "When deciding between %s and %s, both bring distinct advantages depending on your specific goals and environment: "
            "1. %s is typically favored when you prioritize direct simplicity, proven reliability, and classic efficiency in standard scenarios. "
            "2. %s shines when your priority is greater versatility, visual or functional distinction, and tailored adaptability. "
            "The Verdict: Choose %s if you want an effortless, low-friction solution that fits right in. Opt for %s if you want a more expressive or specialized option that stands out. "
            "What is your primary setting or main goal for this choice?",
            capA, capB, capA, capB, capA, capB);
        return 1;
    }

    if (contains(lower, "what is the best") || contains(lower, "which is the best") ||
        contains(lower, "what's the best") || contains(lower, "recommend the best")) {
        const char *p = strstr(lower, "best ");
        if (p) {
            p += 5;
            char category[128] = {0};
            strncpy(category, p, sizeof(category) - 1);
            clean_option_token(category);
            if (strlen(category) > 2) {
                char capCat[128] = {0};
                capitalize_phrase(category, capCat, sizeof(capCat));
                snprintf(out_response, max_len,
                    "To determine the best %s, the optimal choice depends on balancing four key criteria: "
                    "1. Performance and Capability: Delivering top-tier results without unnecessary bottlenecks. "
                    "2. Usability and Ergonomics: Ensuring intuitive daily operation and a smooth experience. "
                    "3. Reliability and Longevity: Standing the test of time with durable construction or strong support. "
                    "4. Value and Budget: Providing the highest return on investment for your specific needs. "
                    "Tell me what your budget and primary use case are, and I will recommend the exact ideal option for you!",
                    category);
                return 1;
            }
        }
    }

    return 0;
}

int brain_handle_direct(const char *user_query, char *out_response, size_t max_len, int *out_is_happy) {
    if (!user_query || !out_response || max_len == 0) return 0;
    out_response[0] = '\0';
    if (out_is_happy) *out_is_happy = 0;

    if (brain_evaluate_math(user_query, out_response, max_len)) {
        return 1;
    }

    char lower[512];
    to_lower_str(user_query, lower, sizeof(lower));

    if (brain_handle_customization_and_personality(lower, out_response, max_len, out_is_happy)) {
        return 1;
    }

    if (brain_handle_conversation(lower, out_response, max_len, out_is_happy)) {
        return 1;
    }

    if (contains(lower, "what time") || contains(lower, "current time") || strcmp(lower, "time") == 0) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%I:%M %p", t);
        const char *display_time = (time_str[0] == '0') ? time_str + 1 : time_str;
        snprintf(out_response, max_len, "The current time is %s.", display_time);
        return 1;
    }
    if (contains(lower, "what day") || contains(lower, "what date") || contains(lower, "today's date") ||
        contains(lower, "what is today") || contains(lower, "current date") || contains(lower, "today date")) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char date_str[128];
        strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", t);
        snprintf(out_response, max_len, "Today is %s.", date_str);
        return 1;
    }

    if (contains(lower, "chat with me") || contains(lower, "can we chat") || contains(lower, "talk to me") ||
        contains(lower, "interactive chat") || strcmp(lower, "can you talk") == 0 || strcmp(lower, "can we talk") == 0) {
        snprintf(out_response, max_len,
            "I am Spike, ready for interactive chats! You can ask me curious science questions like why spiders hang upside down, solve math, learn about space, or explore physical objects. What's on your mind?");
        return 1;
    }
    if (contains(lower, "what can you do") || contains(lower, "what are your capabilities") ||
        contains(lower, "how do you work") || contains(lower, "what do you know")) {
        snprintf(out_response, max_len,
            "I can explain scientific phenomena, teach topics like calculus and physics, answer encyclopedic questions, calculate arithmetic, describe physical appearance, and dynamically chat with you in real time. What would you like to explore?");
        return 1;
    }
    if (contains(lower, "offline") || contains(lower, "without internet") || contains(lower, "need internet") || contains(lower, "no internet")) {
        snprintf(out_response, max_len,
            "I connect to live online knowledge when available, with a resilient native offline database, mathematical engine, and speech system running with 0%% GPU.");
        return 1;
    }

    return 0;
}

int brain_think(const char *user_query, const char *subject_title, const char *web_extract,
                const char *image_caption, const char *image_url,
                char *out_response, size_t max_len, int *out_is_happy) {
    (void)image_url;
    if (!user_query || !out_response || max_len == 0) return 0;
    out_response[0] = '\0';
    if (out_is_happy) *out_is_happy = 0;

    if (brain_handle_direct(user_query, out_response, max_len, out_is_happy)) {
        return 1;
    }

    if (brain_reason_decision(user_query, out_response, max_len, out_is_happy)) {
        return 1;
    }

    char lower[512];
    to_lower_str(user_query, lower, sizeof(lower));

    int is_visual_request = (contains(lower, "look like") ||
                             contains(lower, "looks like") ||
                             contains(lower, "looking like") ||
                             contains(lower, "appearance of") ||
                             contains(lower, "visual appearance") ||
                             contains(lower, "visual description") ||
                             contains(lower, "what does it look like") ||
                             contains(lower, "how does it look") ||
                             contains(lower, "how do they look"));

    if (is_visual_request) {
        brain_describe_visual(user_query, subject_title, image_caption, web_extract, out_response, max_len);
    } else {
        brain_explain_general(user_query, subject_title, web_extract, out_response, max_len);
    }

    return 1;
}
