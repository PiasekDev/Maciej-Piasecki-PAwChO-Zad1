FROM gcc:14.1.0 as builder

# Copy the source code
COPY ./src/server.c .

# Define the DEBUG environment variable
ARG DEBUG

# Compile using gcc, optimize for size, make a static binary, optionally define the DEBUG macro (if DEBUG buildarg is set), strip the binary
RUN gcc -Os -static -o server server.c ${DEBUG:+-D DEBUG} && strip server

FROM scratch as final

# Copy the compiled binary from the builder
COPY --from=builder /server /server

# Describe which port the application is listening on
EXPOSE 80

# Set command to run when the container starts
CMD ["/server"]

# Set healthcheck (adding the "healthcheck" argument to /server will run the healthcheck instead of the server)
HEALTHCHECK --interval=15s --timeout=5s \
	CMD ["/server", "healthcheck"]

# Label with author name, middle name, surname and index number
LABEL org.opencontainers.image.authors="Maciej Krzysztof Piasecki (97701)"
