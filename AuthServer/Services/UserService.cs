using AuthServer.Data;
using AuthServer.Dtos;
using AuthServer.Models;
using Microsoft.EntityFrameworkCore;

namespace AuthServer.Services;

public class UserService
{
    private readonly ApplicationDbContext _dbContext;
    private readonly ILogger<UserService> _logger;

    public UserService(ApplicationDbContext dbContext, ILogger<UserService> logger)
    {
        _dbContext = dbContext;
        _logger = logger;
    }

    public async Task<Result<User>> RegisterAsync(RegisterRequest request)
    {
        if (await _dbContext.Users.AnyAsync(u => u.Username == request.Username))
            return Result<User>.Failure("USERNAME_EXISTS", "이미 존재하는 사용자명입니다.");

        var user = new User
        {
            Username = request.Username,
            PasswordHash = request.Password
        };

        _dbContext.Users.Add(user);

        try
        {
            await _dbContext.SaveChangesAsync();
        }
        catch (DbUpdateException)
        {
            return Result<User>.Failure("USERNAME_EXISTS", "이미 존재하는 사용자명입니다.");
        }

        _logger.LogInformation("새 사용자 등록: {Username}", user.Username);
        return Result<User>.Success(user);
    }

    public async Task AddUserDirectlyAsync(User user)
    {
        _dbContext.Users.Add(user);
        await _dbContext.SaveChangesAsync();
        _logger.LogWarning("{Role} User {Username} Directly Added", user.Role, user.Username);
    }

    public async Task<Result<User>> LoginAsync(LoginRequest request)
    {
        var user = await _dbContext.Users.FirstOrDefaultAsync(u =>
            u.Username == request.Username &&
            u.PasswordHash == request.Password);

        if (user == null)
            return Result<User>.Failure("INVALID_CREDENTIALS", "잘못된 사용자명 또는 비밀번호입니다.");

        return Result<User>.Success(user);
    }

    public Task<User?> GetByIdAsync(string userId)
    {
        return _dbContext.Users.FirstOrDefaultAsync(u => u.Id == userId);
    }

    public Task<User?> GetByUsernameAsync(string username)
    {
        return _dbContext.Users.FirstOrDefaultAsync(u => u.Username == username);
    }
}
