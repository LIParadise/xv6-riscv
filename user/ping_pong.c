#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PING_PONG_LIMIT (1000u * 1000u)
#define BUF_LEN         (32u)

#define my_assert(x)  \
    do                \
    {                 \
        if (!(x))     \
        {             \
            exit(-1); \
        }             \
    } while (0)

#define my_assert_eq(x, y)     \
    do                         \
    {                          \
        my_assert((x) == (y)); \
    } while (0)

#define my_assert_ne(x, y)     \
    do                         \
    {                          \
        my_assert((x) != (y)); \
    } while (0)

/**
 * Assuming `len` accomodates `u` in string form just fine,
 * (plus the implicit null terminator)
 * it's equivalent to `snprintf(buf, "%lu", u, len)`,
 * else the pointed memory are not touched.
 *
 * Return zero iff everything went OK.
 */
int my_unsigned_to_str(char *buf, const uint64 len, const uint64 u)
{
    if ((!buf) || (0 == len))
    {
        return -1;
    }
    else
    {
        uint64 digits = 1;
        for (uint64 v = u; v / 10; v /= 10, ++digits)
        {
        }

        if (digits >= len)
        {
            // return without touching content if cannot fit (including null terminator)
            return -1;
        }
        else
        {
            // if possible, null terminate the string
            buf[digits] = 0;
        }

        for (uint64 d = digits - 1, v = u; d < digits; --d, v /= 10)
        {
            buf[d] = '0' + v % 10;
        }

        return 0;
    }
}

/**
 * If the string represents some valid __decimal unsigned number__,
 * that value is returned, else return value would be zero.
 *
 * In order to differentiate between a valid zero and invalid number,
 * user may supply `success` pointer:
 * if `success` not null, it would store if the translation succeeded.
 */
uint64 my_str_to_unsigned(const char *s, uint8 *success)
{
    uint64 result    = 0;
    uint64 len       = strlen(s);
    uint8  succeeded = 1; // by default assume OK; set to zero if error were to occur

    if (len > 0)
    {
        for (uint64 offset = len - 1, multipier = 1; offset < len; --offset, multipier *= 10)
        {
            if ('0' <= s[offset] && s[offset] <= '9')
            {
                uint64 raw = result + multipier * (s[offset] - '0');
                if (raw < result)
                {
                    // overflow, reject
                    result    = 0;
                    succeeded = 0;
                    break;
                }
                else
                {
                    result = raw;
                }
            }
            else
            {
                // invalid char, reject
                result    = 0;
                succeeded = 0;
                break;
            }
        }

        if ('0' == s[0] && len > 1)
        {
            result    = 0;
            succeeded = 0;
        }
    }
    else
    {
        // empty string is not a valid number
        succeeded = 0;
    }

    if (success)
    {
        *success = succeeded;
    }

    return result;
}

int main()
{
    int child_rx_parent_tx[2];
    int parent_rx_child_tx[2];

    if (pipe(child_rx_parent_tx) != 0)
    {
        exit(1);
    }
    if (pipe(parent_rx_child_tx) != 0)
    {
        exit(1);
    }

    const int fork_result = fork();
    my_assert(fork_result >= 0);

    if (fork_result == 0)
    {
        // child
        close(parent_rx_child_tx[0]);
        close(child_rx_parent_tx[1]);
        // for `read`/`write` syscall, use `dup`
        close(0);
        dup(child_rx_parent_tx[0]);
        close(child_rx_parent_tx[0]);
        close(1);
        dup(parent_rx_child_tx[1]);
        close(parent_rx_child_tx[1]);

        char buf[BUF_LEN] = {0};
        while (read(0, buf, BUF_LEN) > 0)
        {
            uint8        success        = 1;
            const uint64 received_token = my_str_to_unsigned(buf, &success);
            my_assert(success);
            my_assert_eq(my_unsigned_to_str(buf, BUF_LEN, received_token + 1), 0);
            if (strlen(buf) == write(1, buf, strlen(buf)))
            {
                memset(buf, 0, BUF_LEN);
            }
            else
            {
                break;
            }
        }
        exit(0);
    }
    else if (fork_result > 0)
    {
        // parent
        close(parent_rx_child_tx[1]);
        close(child_rx_parent_tx[0]);
        // for `read`/`write` syscall, use `dup`
        close(0);
        dup(parent_rx_child_tx[0]);
        close(parent_rx_child_tx[0]);
        close(1);
        dup(child_rx_parent_tx[1]);
        close(child_rx_parent_tx[1]);

        // parent should initiate the ping pong game
        uint64 ping_pong    = 0;
        uint8  success      = 1;
        char   buf[BUF_LEN] = {0};

        while (ping_pong <= PING_PONG_LIMIT && success)
        {
            my_assert_eq(my_unsigned_to_str(buf, BUF_LEN, ping_pong), 0);
            my_assert_eq(write(1, buf, strlen(buf)), strlen(buf));
            memset(buf, 0, BUF_LEN);
            if (read(0, buf, BUF_LEN) > 0)
            {
                ping_pong = my_str_to_unsigned(buf, &success);
                ping_pong += 1;
            }
            else
            {
                success = 0;
            }
        }

        fprintf(2, "parent: %lu\n", ping_pong);
        return !success;
    }
}
