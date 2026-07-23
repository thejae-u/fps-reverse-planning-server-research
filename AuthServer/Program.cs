using System.Text;
using AuthServer.Data;
using AuthServer.Hubs;
using AuthServer.OpenApi;
using AuthServer.Services;
using AuthServer.Services.Tcp;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.EntityFrameworkCore;
using Microsoft.IdentityModel.Tokens;
using Microsoft.OpenApi;
using Scalar.AspNetCore;
using StackExchange.Redis;
using Serilog;

Log.Logger = new LoggerConfiguration().MinimumLevel.Information().WriteTo.Console().CreateLogger();

Log.Information("Server Starting...");
var builder = WebApplication.CreateBuilder(args);

builder.Host.UseSerilog();

builder.Services.AddControllers();

// PostgreSQL configuration
var dbConnectionString = builder.Configuration.GetConnectionString("DefaultConnection");
builder.Services.AddDbContext<ApplicationDbContext>(options => options.UseNpgsql(dbConnectionString));

// Redis Configuration
var redisConnectionString = builder.Configuration["Redis:ConnectionString"] ?? builder.Configuration["Redis__ConnectionString"] ?? "redis:6379";
builder.Services.AddSingleton<IConnectionMultiplexer>(ConnectionMultiplexer.Connect(redisConnectionString));

// Cors for dev
builder.Services.AddCors(options =>
{
    options.AddPolicy("DevCors", policy =>
    {
        policy
            .SetIsOriginAllowed(_ => true)
            .AllowAnyHeader()
            .AllowAnyMethod()
            .AllowCredentials();
    });
});

builder.Services.AddOpenApi("v1", options =>
{
    options.AddDocumentTransformer<BearerSecuritySchemeTransformer>();
});

// web socket
builder.Services.AddSignalR();


// global fields
builder.Services.Configure<MatchOptions>(builder.Configuration.GetSection("MatchOptions"));
builder.Services.Configure<TcpOptions>(builder.Configuration.GetSection("LogicServer"));

// Service DI
builder.Services.AddSingleton<DedicatedServerSpawner>();
builder.Services.AddSingleton<IDedicatedServerSpawner>(sp => sp.GetRequiredService<DedicatedServerSpawner>());
builder.Services.AddSingleton<UserService>();
builder.Services.AddSingleton<JwtTokenService>();
builder.Services.AddSingleton<MatchService>();
builder.Services.AddLogging();
builder.Services.AddTransient<DataSeeder>();

// background internal service
builder.Services.AddHostedService<MatchWorker>();

// JWT Configuration
var jwtIssuer = builder.Configuration["Jwt:Issuer"];
var jwtAudience = builder.Configuration["Jwt:Audience"];
var jwtSecretKey = builder.Configuration["Jwt:SecretKey"];

if (string.IsNullOrEmpty(jwtIssuer) || string.IsNullOrEmpty(jwtAudience) || string.IsNullOrEmpty(jwtSecretKey))
{
    throw new InvalidOperationException("JWT appsettings not set");
}

builder.Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme).AddJwtBearer(options =>
{
    options.TokenValidationParameters = new TokenValidationParameters
    {
        ValidateIssuer = true,
        ValidIssuer = jwtIssuer,
        ValidateAudience = true,
        ValidAudience = jwtAudience,
        ValidateIssuerSigningKey = true,
        IssuerSigningKey = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(jwtSecretKey)),
        ValidateLifetime = true,
        ClockSkew = TimeSpan.Zero
    };

    // SignalR Hubs Authorize
    options.Events = new JwtBearerEvents
    {
        OnMessageReceived = context =>
        {
            var accessToken = context.Request.Query["access_token"];
            var path = context.HttpContext.Request.Path;

            if (!string.IsNullOrEmpty(accessToken) && path.StartsWithSegments("/hubs/match"))
            {
                context.Token = accessToken;
            }

            return Task.CompletedTask;
        }
    };
});

builder.Services.AddAuthorization();

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
    app.MapScalarApiReference("/scalar", options =>
    {
        options.WithTitle("AuthServer API Reference");
    });
}

app.UseCors("DevCors");

app.UseAuthentication();
app.UseAuthorization();

app.MapControllers();

// Server Information Route
app.MapGet("/ping", () => "AuthServer v1.0 - OK");
app.MapGet("/health", () => new { status = "healthy", timestamp = DateTime.UtcNow });
app.MapGet("/version", () => "AuthServer v0.11.0-develop");
app.MapGet("/version/detail", () => new
{
    Version = "version 0.11.0",
    Status = "feature",
    Implement = "dedicated server implement",
    FeatureBranch = new
    {
        Name = "feat/12-impl-dedicated-process",
        Link = "https://thejaeu.com/fps-reverse-planning-server-research/tree/feat/12-impl-dedicated-process"
    }
});
app.MapGet("/info", () => new
{
    info = "API Server for Native C++ Game Logic Server",
    detail = "Created thejaeu with AI"
});

// SignalR Match Hub Route
app.MapHub<MatchHub>("/hubs/match");

// 구동 직전 admin, internal 계정 생성
using (var scope = app.Services.CreateScope())
{
    var seeder = scope.ServiceProvider.GetRequiredService<DataSeeder>();
    await seeder.SeedAsync();
}
app.Run();
