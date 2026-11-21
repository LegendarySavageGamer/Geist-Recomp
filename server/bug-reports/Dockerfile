FROM alpine:3.22
RUN apk add --no-cache ca-certificates && adduser -D -u 10001 geist
COPY dist/linux-amd64/GeistReportService /usr/local/bin/geist-report-service
USER geist
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/geist-report-service"]
